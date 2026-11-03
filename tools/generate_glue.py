#!/usr/bin/env python3
"""Generate the table-driven Thumb executors (src/cpu/thumb_gen16.c and
thumb_gen32.c) from tools/glue_table.py.

Each table entry becomes one executor: it reads the encoding's fields with
the generated decoder accessors, checks the UNPREDICTABLE condition (returns
STD_E_INVALID), fills the pseudo-code's input structure and calls the
athrill-arm-common function. The entry's text from the Armv7-M manual is the
executor's comment. An encoding must not have a hand-written executor in
thumb_exec16.c / thumb_exec32.c as well.

Usage:
  python tools/generate_glue.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import glue_table  # noqa: E402

# kind: (input type, output type, register members, header)
KINDS = {
    "ls_imm": ("arm_ls_imm_input_type", "arm_ls_output_type", ("Rt", "Rn"), None),
    "ls_reg": ("arm_ls_reg_input_type", "arm_ls_output_type", ("Rt", "Rn", "Rm"), None),
    "ls_lit": ("arm_ls_lit_input_type", "arm_ls_output_type", ("Rt",), None),
    "ls_dual": ("arm_ls_dual_input_type", "arm_ls_output_type", ("Rt", "Rt2", "Rn"), None),
    "ls_multiple": ("arm_ls_multiple_input_type", "arm_ls_output_type", ("Rn",), None),
    "ls_exclusive": ("arm_ls_exclusive_input_type", "arm_ls_output_type", ("Rd", "Rt", "Rn"), None),
    "tb": ("arm_tb_input_type", "arm_b_output_type", ("Rn", "Rm"), None),
    "bl": ("arm_b_input_type", "arm_b_output_type", (), None),
    "blx": ("arm_blx_reg_input_type", "arm_b_output_type", ("Rm",), None),
    "cb": ("arm_cb_input_type", "arm_b_output_type", ("Rn",), None),
    "dp_imm": ("arm_dp_imm_input_type", "arm_dp_output_type", ("Rd", "Rn"), None),
    "dp_reg": ("arm_dp_reg_input_type", "arm_dp_output_type", ("Rd", "Rn", "Rm"), None),
    "adr": ("arm_adr_input_type", "arm_dp_output_type", ("Rd",), None),
    "extend": ("arm_extend_input_type", "arm_dp_output_type", ("Rd", "Rn", "Rm"), None),
    "bfx": ("arm_bfx_input_type", "arm_dp_output_type", ("Rd", "Rn"), None),
    "bfi": ("arm_bfi_input_type", "arm_dp_output_type", ("Rd", "Rn"), None),
    "unary": ("arm_unary_input_type", "arm_dp_output_type", ("Rd", "Rm"), None),
    "mul": ("arm_mul_input_type", "arm_dp_output_type", ("Rd", "Rn", "Rm", "Ra"), None),
    "mul_long": ("arm_mul_long_input_type", "arm_mul_long_output_type", ("RdLo", "RdHi", "Rn", "Rm"), None),
}
# Kinds whose instructions are not conditional (no ConditionPassed()).
NO_COND = {"cb"}
HEADERS = ["arm_common/arm_pseudo_load.h", "arm_common/arm_pseudo_store.h", "arm_common/arm_pseudo_multiple.h",
           "arm_common/arm_pseudo_exclusive.h", "arm_common/arm_pseudo_branch.h", "arm_common/arm_pseudo_extend.h",
           "arm_common/arm_pseudo_bitfield.h", "arm_common/arm_pseudo_multiply.h"]


def decoder_ids(width):
    path = os.path.join(ROOT, "src", "cpu", "decoder", "t32_decode%d.h" % width)
    return set(re.findall(r"\tT32_%d_(\w+) = \d+," % width, open(path).read()))


def hand_ids(width):
    path = os.path.join(ROOT, "src", "cpu", "thumb_exec%d.c" % width)
    return set(re.findall(r"\[T32_%d_(\w+)\] = exec_" % width, open(path).read()))


def fields(text, id, width):
    prefix = "t32w" if width == 32 else "t32"
    return re.sub(r"\{(\w+)\}", lambda m: "%s_%s_%s(insn)" % (prefix, id, m.group(1)), text)


def strip_outer(expr):
    """(a == b) -> a == b, when the parentheses enclose the whole expression."""
    if not (expr.startswith("(") and expr.endswith(")")):
        return expr
    depth = 0
    for i, ch in enumerate(expr):
        depth += (ch == "(") - (ch == ")")
        if depth == 0 and i != len(expr) - 1:
            return expr
    return expr[1:-1]


def comment(e):
    lines = ["%s: %s" % (e.id, e.syntax)]
    lines += e.see
    lines.append(e.ops)
    if e.unpred:
        # "a; b": two conditions, one line each as in the manual
        lines += ["if %s then UNPREDICTABLE;" % u for u in e.unpred.split("; ")]
    if e.unpred and e.unpred_c is None:
        lines.append("(InITBlock() is FALSE while IT is not executed: nothing to check here.)")
    if e.note:
        lines.append(e.note)
    return "/* " + "\n *     ".join(lines) + " */\n"


def executor(e, width):
    insn_type = "uint32" if width == 32 else "uint16"
    name = "exec_" + e.id.lower()
    body = []
    for var, expr in e.regs:
        body.append("\tuint32 %s = %s;\n" % (var, fields(expr, e.id, width)))
    if e.kind in KINDS:
        in_type, out_type, _, _ = KINDS[e.kind]
        body.append("\t%s in;\n\t%s out;\n" % (in_type, out_type))
    if not e.regs and e.kind not in KINDS:
        body.append("\t(void)insn;\n")
    if body:
        body.append("\n")
    if e.unpred_c:
        body.append("\tif (%s) {\n\t\treturn STD_E_INVALID;\n\t}\n" % fields(strip_outer(e.unpred_c), e.id, width))
    if e.kind == "hint":
        body.append("\t(void)core;\n\treturn STD_E_OK;\n")
    elif e.kind == "clrex":
        body.append("\treturn arm_pseudo_clrex(core, Stm32CurrentCond(core));\n")
    else:
        _, _, reg_members, _ = KINDS[e.kind]
        body.append('\tin.instrName = "%s";\n' % e.instr)
        if e.kind not in NO_COND:
            body.append("\tin.cond = Stm32CurrentCond(core);\n")
        for member, expr in e.members.items():
            if member in reg_members:
                if expr is None:
                    body.append("\tARM_NO_REG(&in, %s);\n" % member)
                else:
                    body.append("\tARM_SET_REG(core, &in, %s, %s);\n" % (member, fields(expr, e.id, width)))
            else:
                body.append("\tin.%s = %s;\n" % (member, fields(expr, e.id, width)))
        body.append("\treturn arm_pseudo_%s(core, &in, &out);\n" % e.call)
    return name, comment(e) + "static Std_ReturnType %s(TargetCoreType *core, %s insn)\n{\n%s}\n" % (
        name, insn_type, "".join(body))


def main():
    ids = {16: decoder_ids(16), 32: decoder_ids(32)}
    hand = {16: hand_ids(16), 32: hand_ids(32)}
    out = {16: [], 32: []}
    tables = {16: [], 32: []}
    seen = set()
    for e in glue_table.ENTRIES:
        width = 16 if e.id in ids[16] else 32 if e.id in ids[32] else None
        if width is None:
            sys.exit("%s: not a generated decoder id" % e.id)
        if e.id in seen:
            sys.exit("%s: two table entries" % e.id)
        if e.id in hand[width]:
            sys.exit("%s: has a hand-written executor in thumb_exec%d.c" % (e.id, width))
        seen.add(e.id)
        name, code = executor(e, width)
        out[width].append(code)
        tables[width].append("\t[T32_%d_%s] = %s,\n" % (width, e.id, name))
    for width in (16, 32):
        text = ["/*\n * Generated by tools/generate_glue.py from tools/glue_table.py. Do not edit.\n *\n"
                " * %d-bit Thumb executors of the table: each encoding's fields become the\n"
                " * inputs of athrill-arm-common's pseudo-code.\n */\n" % width,
                '#include "thumb_glue.h"\n#include "t32_decode%d.h"\n' % width]
        text += ['#include "%s"\n' % h for h in HEADERS]
        text.append("\n")
        text.append("\n".join(out[width]))
        text.append("\nconst ThumbExec%d thumb_gen_executors%d[T32_%d_ID_NUM] = {\n" % (width, width, width))
        text += tables[width]
        text.append("};\n")
        with open(os.path.join(ROOT, "src", "cpu", "thumb_gen%d.c" % width), "w") as f:
            f.write("".join(text))
    print("%d 16-bit and %d 32-bit executors" % (len(tables[16]), len(tables[32])))


if __name__ == "__main__":
    main()

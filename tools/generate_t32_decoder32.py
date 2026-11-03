#!/usr/bin/env python3
"""Generate the 32-bit Thumb decoder of Athrill's STM32 target from Arm's
machine-readable architecture data (AARCHMRS, Instructions.json).

32-bit instructions cannot be enumerated like the 16-bit ones, so the T32
tree itself becomes C code: one function per instruction group, which tests
each child's fixed bits and decode condition in the data's order and returns
the first instruction that matches. The word is the first halfword in bits
31:16 and the second in bits 15:0, as in the data.

Armv7-M rules for Cortex-M7 (Armv7E-M with FPv5, see docs/decoder.md):
should-be bits do not take part in decoding; IsFeatureImplemented() is false;
the groups in NOT_IN_ARMV7M_GROUPS and the instructions in NOT_IN_ARMV7M,
which Armv7-M does not have, decode as undefined; the instructions in
ARMV7M_AS execute as another instruction in Armv7-M; the instructions in
ARMV7M_FIXED have more fixed bits in Armv7-M.

Usage:
  python tools/generate_t32_decoder32.py <path to Instructions.json> <package name>
writes src/cpu/decoder/t32_decode32.h and t32_decode32.c.
"""
import hashlib
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from generate_t32_decoder import NOTICE, OUT_DIR, decode_pattern, encoding_fields, strip  # noqa: E402

# Groups Armv7-M does not have: Advanced SIMD (NEON) and its extensions,
# Armv8 load-acquire/store-release, and A-profile system instructions
# (32-bit CPS, BXJ, ERET, DCPS, BLX immediate).
NOT_IN_ARMV7M_GROUPS = {
    "simddp", "vldst", "simd_3sameext", "tfloatdpmac", "tsimd_dotprod",
    "ldastl", "cps", "bx_jaz", "eret", "dcps", "blx",
}

# Instructions Armv7-M does not have, in groups it does have.
NOT_IN_ARMV7M = {
    "STREXD_T1", "LDREXD_T1",
    "SRS_T1_AS", "SRS_T2_AS", "RFE_T1_AS", "RFE_T2_AS", "HVC_T1", "SMC_T1_AS", "SB_T1",
    "CRC32B_T1", "CRC32H_T1", "CRC32W_T1", "CRC32CB_T1", "CRC32CH_T1", "CRC32CW_T1",
    "PLDW_r_T1", "PLDW_i_T1", "PLDW_i_T2",
    "FSTMDBX_T1", "FSTMIAX_T1", "FLDMDBX_T1", "FLDMIAX_T1", "VDUP_r_T1_Q", "VDUP_r_T1_D",
    "VJCVT_T1", "VCVTB_T1_bfs", "VCVTT_T1_bfs", "VMOV_toh_T1", "VMOV_h_T1", "VMOVX_T1", "VINS_T1",
}


# Extra fixed bits in Armv7-M: VMOV between a core register and a scalar
# moves only a 32-bit word (U = 0, opc1<1> = 0, opc2 = 00); the 8- and 16-bit
# forms are Advanced SIMD.
ARMV7M_FIXED = {
    "VMOV_rs_T1": (0x00C00060, 0x00000000),
    "VMOV_sr_T1": (0x00C00060, 0x00000000),
}


def not_in_armv7m(name):
    """Half-precision arithmetic (names ending _H) is Armv8.2; Cortex-M7 has
    only the half-precision conversions (VCVTB/VCVTT)."""
    return name in NOT_IN_ARMV7M or name.endswith("_H")


# Instructions Armv7-M executes as another one: unallocated hints are NOP,
# DSB with any option is DSB, and the banked-register MRS/MSR forms are
# MRS/MSR with a SYSm value (bits 7:0) Armv7-M reserves.
ARMV7M_AS = {
    "MRS_br_T1_AS": "MRS_T1_AS", "MSR_br_T1_AS": "MSR_r_T1_AS",
    "SEVL_T2": "NOP_T2", "ESB_T1": "NOP_T2", "TSB_T1": "NOP_T2", "CSDB_T1": "NOP_T2",
    "CLRBHB_T1": "NOP_T2", "SSBB_T1": "DSB_T1", "PSSBB_T1": "DSB_T1",
}


def mask_match(pattern, start):
    mask = match = 0
    width = len(pattern)
    for i, ch in enumerate(pattern):
        bit = start + width - 1 - i
        if ch in "01":
            mask |= 1 << bit
            match |= (ch == "1") << bit
    return mask, match


def encoding_test(encoding):
    mask = match = 0
    for value in encoding["values"]:
        m, v = mask_match(decode_pattern(value), value["range"]["start"])
        mask |= m
        match |= v
    if mask == 0:
        return None
    return "((insn & 0x%08XU) == 0x%08XU)" % (mask, match)


class ConditionToC:
    """A decode condition (AST) as a C expression over insn."""

    def __init__(self, fields):
        self.fields = fields

    def field(self, name, high=None, low=None):
        start, width = self.fields[name]
        if high is not None:
            start, width = start + low, high - low + 1
        return start, width

    def compare(self, start, width, pattern):
        m, v = mask_match(pattern, start)
        assert len(pattern) == width, (pattern, width)
        if m == 0:
            return "1"
        return "((insn & 0x%08XU) == 0x%08XU)" % (m, v)

    def operand(self, node):
        if node["_type"] == "AST.Identifier":
            return self.field(node["value"])
        if node["_type"] == "AST.SquareOp":
            arg = node["arguments"][0]
            if arg["_type"] == "AST.Slice":
                return self.field(node["var"]["value"], arg["left"]["value"], arg["right"]["value"])
            return self.field(node["var"]["value"], arg["value"], arg["value"])
        raise ValueError("unsupported operand: " + json.dumps(node)[:200])

    def c(self, node):
        kind = node["_type"]
        if kind == "AST.Bool":
            return "1" if node["value"] else "0"
        if kind == "AST.UnaryOp" and node["op"] == "!":
            return "!" + self.c(node["expr"])
        if kind == "AST.Function" and node["name"] == "IsFeatureImplemented":
            return "0 /* %s */" % node["arguments"][0]["value"]
        if kind == "AST.BinaryOp":
            op = node["op"]
            if op in ("&&", "||"):
                return "(%s %s %s)" % (self.c(node["left"]), op, self.c(node["right"]))
            start, width = self.operand(node["left"])
            if op in ("==", "!="):
                test = self.compare(start, width, strip(node["right"]["value"]))
                return test if op == "==" else "!" + test
            if op == "IN":
                tests = [self.compare(start, width, strip(v["value"])) for v in node["right"]["values"]]
                return "(" + " || ".join(tests) + ")"
        raise ValueError("unsupported condition: " + json.dumps(node)[:200])


def c_name(name):
    return re.sub(r"[^A-Za-z0-9_]", "_", name)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    path, package = sys.argv[1], sys.argv[2]
    with open(path, "rb") as f:
        raw = f.read()
    sha256 = hashlib.sha256(raw).hexdigest()
    data = json.loads(raw)
    t32 = next(s for s in data["instructions"] if s["name"] == "T32")
    wide = next(c for c in t32["children"] if c["name"] == "w")

    leaves = []	# (name, fields)
    functions = []	# C functions, innermost first

    def node_test(node, fields):
        tests = []
        enc = encoding_test(node["encoding"])
        if enc:
            tests.append(enc)
        cond = node.get("condition")
        if cond is not None and not (cond["_type"] == "AST.Bool" and cond["value"]):
            tests.append(ConditionToC(fields).c(cond))
        return " && ".join(tests) if tests else "1"

    def emit_group(node, inherited):
        fields = dict(inherited)
        fields.update(encoding_fields(node["encoding"]))
        body = []
        for child in node.get("children", []):
            child_fields = dict(fields)
            child_fields.update(encoding_fields(child["encoding"]))
            test = node_test(child, child_fields)
            if child["name"] in ARMV7M_FIXED:
                test += " && ((insn & 0x%08XU) == 0x%08XU)" % ARMV7M_FIXED[child["name"]]
            if child["_type"] == "Instruction.Instruction" and not_in_armv7m(child["name"]):
                continue
            if child["_type"] == "Instruction.Instruction" and child["name"] in ARMV7M_AS:
                body.append("\tif (%s) {\n\t\treturn T32_32_%s;\t/* %s in Armv7-M */\n\t}"
                            % (test, ARMV7M_AS[child["name"]], child["name"]))
            elif child["_type"] == "Instruction.Instruction":
                leaves.append((child["name"], [(v["name"], v["range"]["start"], v["range"]["width"])
                                                for v in child["encoding"]["values"]
                                                if v["_type"] == "Instruction.Encodeset.Field"
                                                and "x" in strip(v["value"]["value"])]))
                body.append("\tif (%s) {\n\t\treturn T32_32_%s;\n\t}" % (test, c_name(child["name"])))
            elif child["name"] in NOT_IN_ARMV7M_GROUPS:
                body.append("\tif (%s) {\n\t\treturn T32_32_UNDEFINED;\t/* %s: not in Armv7-M */\n\t}"
                            % (test, child["name"]))
            else:
                emit_group(child, child_fields)
                body.append("\tif (%s) {\n\t\tT32Decode32Id id = decode_%s(insn);\n\t\tif (id != T32_32_UNDEFINED) {\n\t\t\treturn id;\n\t\t}\n\t}"
                            % (test, c_name(child["name"])))
        functions.append("static T32Decode32Id decode_%s(uint32_t insn)\n{\n\t(void)insn;\n%s\n\treturn T32_32_UNDEFINED;\n}\n"
                         % (c_name(node["name"]), "\n".join(body)))

    root_fields = encoding_fields(wide["encoding"])
    emit_group(wide, root_fields)
    names = ["UNDEFINED"] + [c_name(n) for n, _ in leaves]
    if len(set(names)) != len(names):
        sys.exit("duplicate instruction names")

    notice = NOTICE.format(package=package, sha256=sha256)
    header = ["/*", " * Generated by tools/generate_t32_decoder32.py. Do not edit.", " *",
              " * 32-bit Thumb (T32) decoder: t32_decode32() gives the instruction id of",
              " * a word whose bits 31:16 are the first halfword and 15:0 the second.", " *",
              notice, " */", "#ifndef T32_DECODE32_H", "#define T32_DECODE32_H", "",
              "#include <stdint.h>", "", "typedef enum {"]
    header += ["\tT32_32_%s = %d," % (n, i) for i, n in enumerate(names)]
    header += ["\tT32_32_ID_NUM = %d" % len(names), "} T32Decode32Id;", "",
               "extern const char *const t32_decode32_names[T32_32_ID_NUM];",
               "T32Decode32Id t32_decode32(uint32_t insn);", "",
               "/* Fields of each instruction: t32w_<instruction>_<field>(insn). */"]
    for name, fields in leaves:
        for field, start, width in fields:
            header.append("static inline uint32_t t32w_%s_%s(uint32_t insn) { return (insn >> %d) & 0x%XU; }"
                          % (c_name(name), field, start, (1 << width) - 1))
    header += ["", "#endif", ""]

    source = ["/*", " * Generated by tools/generate_t32_decoder32.py. Do not edit.", " *", notice, " */",
              '#include "t32_decode32.h"', "", "const char *const t32_decode32_names[T32_32_ID_NUM] = {"]
    source += ['\t"%s",' % n for n in names]
    source += ["};", ""] + functions
    source += ["T32Decode32Id t32_decode32(uint32_t insn)", "{",
               "\treturn decode_%s(insn);" % c_name(wide["name"]), "}", ""]

    with open(os.path.join(OUT_DIR, "t32_decode32.h"), "w") as f:
        f.write("\n".join(header))
    with open(os.path.join(OUT_DIR, "t32_decode32.c"), "w") as f:
        f.write("\n".join(source))
    print("%d instructions in %d group functions" % (len(leaves), len(functions)))


if __name__ == "__main__":
    main()

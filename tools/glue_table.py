"""The glue table: for each Thumb encoding the generated decoders give, how
its fields become the inputs of athrill-arm-common's pseudo-code.

Each entry is one encoding (a generated decoder id). Its text (syntax, SEE
lines, the "Encoding specific operations" and the UNPREDICTABLE condition)
is that of the Armv7-M Architecture Reference Manual; the generated executor
carries it as its comment. In the C expressions, {field} is the encoding's
field (the generated accessor), and the local variables of `regs` (t, n, m,
d, t2) can be used.

Entries are made with the functions below; tools/generate_glue.py writes
src/cpu/thumb_gen16.c and thumb_gen32.c from them.
"""

IN_IT_LAST = "InITBlock() && !LastInITBlock()"


class Entry:
    def __init__(self, id, instr, syntax, ops, kind, call, regs, members,
                 see=(), unpred=None, unpred_c=None, note=None):
        self.id = id
        self.instr = instr
        self.syntax = syntax
        self.ops = ops
        self.kind = kind          # input structure (see generate_glue.KINDS)
        self.call = call          # pseudo-code function, or None (a hint)
        self.regs = regs          # [(variable, C expression)]
        self.members = members    # {member: C expression}
        self.see = list(see)
        self.unpred = unpred
        self.unpred_c = unpred_c
        self.note = note


ENTRIES = []


def entry(*args, **kwargs):
    ENTRIES.append(Entry(*args, **kwargs))


# ---------------------------------------------------------------------------
# Single loads and stores, immediate offset
# ---------------------------------------------------------------------------

def ls_imm(id, instr, call, syntax, ops, t, n, imm32, index="TRUE", add="TRUE", wback="FALSE", **kw):
    entry(id, instr, syntax, ops, "ls_imm", call, [("t", t), ("n", n)],
          {"Rt": "t", "Rn": "n", "imm32": imm32, "index": index, "add": add, "wback": wback}, **kw)


def ls_reg(id, instr, call, syntax, ops, t, n, m, shift_n="0U", **kw):
    entry(id, instr, syntax, ops, "ls_reg", call, [("t", t), ("n", n), ("m", m)],
          {"Rt": "t", "Rn": "n", "Rm": "m", "shift_t": "SRType_LSL", "shift_n": shift_n,
           "index": "TRUE", "add": "TRUE", "wback": "FALSE"}, **kw)


def ls_lit(id, instr, call, syntax, ops, t, imm32, add, **kw):
    entry(id, instr, syntax, ops, "ls_lit", call, [("t", t)],
          {"Rt": "t", "imm32": imm32, "add": add}, **kw)


# 16-bit
O_T1_RTRN = "t = UInt(Rt); n = UInt(Rn); index = TRUE; add = TRUE; wback = FALSE;"
O_T1_REG = ("t = UInt(Rt); n = UInt(Rn); m = UInt(Rm); index = TRUE; add = TRUE; wback = FALSE;"
            " (shift_t, shift_n) = (SRType_LSL, 0);")

ls_imm("LDR_i_T1", "LDR", "ldr_imm", "LDR <Rt>, [<Rn>{, #<imm5:'00'>}]",
       "imm32 = ZeroExtend(imm5:'00', 32); " + O_T1_RTRN, "{Rt}", "{Rn}", "{imm5} << 2")
ls_imm("LDR_i_T2", "LDR", "ldr_imm", "LDR <Rt>, [SP{, #<imm8:'00'>}]",
       "t = UInt(Rt); n = 13; imm32 = ZeroExtend(imm8:'00', 32); index = TRUE; add = TRUE; wback = FALSE;",
       "{Rt}", "13U", "{imm8} << 2")
ls_imm("LDRB_i_T1", "LDRB", "ldrb_imm", "LDRB <Rt>, [<Rn>{, #<imm5>}]",
       "imm32 = ZeroExtend(imm5, 32); " + O_T1_RTRN, "{Rt}", "{Rn}", "{imm5}")
ls_imm("LDRH_i_T1", "LDRH", "ldrh_imm", "LDRH <Rt>, [<Rn>{, #<imm5:'0'>}]",
       "imm32 = ZeroExtend(imm5:'0', 32); " + O_T1_RTRN, "{Rt}", "{Rn}", "{imm5} << 1")
ls_imm("STR_i_T1", "STR", "str_imm", "STR <Rt>, [<Rn>{, #<imm5:'00'>}]",
       "imm32 = ZeroExtend(imm5:'00', 32); " + O_T1_RTRN, "{Rt}", "{Rn}", "{imm5} << 2")
ls_imm("STR_i_T2", "STR", "str_imm", "STR <Rt>, [SP, #<imm8:'00'>]",
       "t = UInt(Rt); n = 13; imm32 = ZeroExtend(imm8:'00', 32); index = TRUE; add = TRUE; wback = FALSE;",
       "{Rt}", "13U", "{imm8} << 2")
ls_imm("STRB_i_T1", "STRB", "strb_imm", "STRB <Rt>, [<Rn>, #<imm5>]",
       "imm32 = ZeroExtend(imm5, 32); " + O_T1_RTRN, "{Rt}", "{Rn}", "{imm5}")
ls_imm("STRH_i_T1", "STRH", "strh_imm", "STRH <Rt>, [<Rn>{, #<imm5:'0'>}]",
       "imm32 = ZeroExtend(imm5:'0', 32); " + O_T1_RTRN, "{Rt}", "{Rn}", "{imm5} << 1")
ls_lit("LDR_l_T1", "LDR", "ldr_lit", "LDR <Rt>, <label>",
       "t = UInt(Rt); imm32 = ZeroExtend(imm8:'00', 32); add = TRUE;", "{Rt}", "{imm8} << 2", "TRUE")
for name, call in (("LDR", "ldr_reg"), ("LDRB", "ldrb_reg"), ("LDRH", "ldrh_reg"), ("LDRSB", "ldrsb_reg"),
                   ("LDRSH", "ldrsh_reg"), ("STR", "str_reg"), ("STRB", "strb_reg"), ("STRH", "strh_reg")):
    ls_reg(name + "_r_T1", name, call, "%s <Rt>, [<Rn>, <Rm>]" % name, O_T1_REG, "{Rt}", "{Rn}", "{Rm}")

# 32-bit, 12-bit immediate (offset form)
LOADS32 = [
    # instr, call, imm12 encoding, Rt == 1111 sees, t unpredictable (imm12 and literal forms)
    ("LDR", "ldr", "LDR_i_T3", None, "t == 15 && " + IN_IT_LAST),
    ("LDRB", "ldrb", "LDRB_i_T2", "PLD", "t == 13"),
    ("LDRH", "ldrh", "LDRH_i_T2", "Related encodings (memory hints)", "t == 13"),
    ("LDRSB", "ldrsb", "LDRSB_i_T1", "PLI", "t == 13"),
    ("LDRSH", "ldrsh", "LDRSH_i_T1", "Related encodings (memory hints)", "t == 13"),
]
UNPRED_C_T = {
    "t == 15 && " + IN_IT_LAST: None,     # InITBlock() is FALSE while IT is not executed
    "t == 13": "(t == 13U)",
}
for instr, call, id, see_pld, unpred in LOADS32:
    see = []
    if see_pld:
        see.append("if Rt == '1111' then SEE %s;" % see_pld)
    see.append("if Rn == '1111' then SEE %s (literal);" % instr)
    ls_imm(id, instr, call + "_imm", "%s<c>.W <Rt>, [<Rn>{, #<imm12>}]" % instr,
           "t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm12, 32); index = TRUE; add = TRUE; wback = FALSE;",
           "{Rt}", "{Rn}", "{imm12}", see=see, unpred=unpred, unpred_c=UNPRED_C_T[unpred])

for instr, call, id in (("STR", "str", "STR_i_T3"), ("STRB", "strb", "STRB_i_T2"), ("STRH", "strh", "STRH_i_T2")):
    unpred = "t == 15" if instr == "STR" else "t IN {13,15}"
    unpred_c = "(t == 15U)" if instr == "STR" else "thumb_sp_or_pc(t)"
    ls_imm(id, instr, call + "_imm", "%s<c>.W <Rt>, [<Rn>, #<imm12>]" % instr,
           "t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm12, 32); index = TRUE; add = TRUE; wback = FALSE;",
           "{Rt}", "{Rn}", "{imm12}", see=["if Rn == '1111' then UNDEFINED;"], unpred=unpred, unpred_c=unpred_c)

# 32-bit, 8-bit immediate with P, U, W: one generated id per form
#   _off (P = 1, U = 0, W = 0: [Rn, #-imm8]); _post (P = 0, W = 1); _pre (P = 1, W = 1)
FORMS = {
    "off": ("[<Rn>, #-<imm8>]", "TRUE", "FALSE", "FALSE"),
    "offn": ("[<Rn>, #-<imm8>]", "TRUE", "FALSE", "FALSE"),
    "post": ("[<Rn>], #+/-<imm8>", "FALSE", "{U} != 0U", "TRUE"),
    "pre": ("[<Rn>, #+/-<imm8>]!", "TRUE", "{U} != 0U", "TRUE"),
}
P_U_W_OPS = ("t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm8, 32);"
             " index = (P == '1'); add = (U == '1'); wback = (W == '1');")
LOADS8 = [
    # instr, call, id prefix, SEE for Rt = 1111 (offset form), unprivileged instruction
    ("LDR", "ldr", "LDR_i_T4", None, "LDRT"),
    ("LDRB", "ldrb", "LDRB_i_T3", "PLD", "LDRBT"),
    ("LDRH", "ldrh", "LDRH_i_T3", "Related encodings (memory hints)", "LDRHT"),
    ("LDRSB", "ldrsb", "LDRSB_i_T2", "PLI", "LDRSBT"),
    ("LDRSH", "ldrsh", "LDRSH_i_T2", "Related encodings (memory hints)", "LDRSHT"),
]
for instr, call, prefix, see_pld, unpriv in LOADS8:
    for form in ("off", "post", "pre"):
        operand, index, add, wback = FORMS[form]
        see = []
        if see_pld:
            see.append("if Rt == '1111' && P == '1' && U == '0' && W == '0' then SEE %s;" % see_pld)
        see += ["if Rn == '1111' then SEE %s (literal);" % instr,
                "if P == '1' && U == '1' && W == '0' then SEE %s;" % unpriv]
        if instr == "LDR":
            see.append("if Rn == '1101' && P == '0' && U == '1' && W == '1' && imm8 == '00000100' then SEE POP;")
        see.append("if P == '0' && W == '0' then UNDEFINED;")
        if instr == "LDR":
            unpred = "(wback && n == t) || (t == 15 && %s)" % IN_IT_LAST
            unpred_c = "(n == t)" if wback == "TRUE" else None
        else:
            unpred = "t == 13 || (t == 15 && W == '1') || (wback && n == t)"
            unpred_c = "(t == 13U) || (t == 15U) || (n == t)" if wback == "TRUE" else "(t == 13U)"
        ls_imm("%s_%s" % (prefix, form), instr, call + "_imm", "%s<c> <Rt>, %s" % (instr, operand), P_U_W_OPS,
               "{Rt}", "{Rn}", "{imm8}", index, add, wback, see=see, unpred=unpred, unpred_c=unpred_c)

STORES8 = [("STR", "str", "STR_i_T4", "STRT", "off"), ("STRB", "strb", "STRB_i_T3", "STRBT", "offn"),
           ("STRH", "strh", "STRH_i_T3", "STRHT", "offn")]
for instr, call, prefix, unpriv, offname in STORES8:
    for form in (offname, "post", "pre"):
        operand, index, add, wback = FORMS[form]
        see = ["if P == '1' && U == '1' && W == '0' then SEE %s;" % unpriv]
        if instr == "STR":
            see.append("if Rn == '1101' && P == '1' && U == '0' && W == '1' && imm8 == '00000100' then SEE PUSH;")
        see.append("if Rn == '1111' || (P == '0' && W == '0') then UNDEFINED;")
        if instr == "STR":
            unpred = "t == 15 || (wback && n == t)"
            unpred_c = "(t == 15U) || (n == t)" if wback == "TRUE" else "(t == 15U)"
        else:
            unpred = "t IN {13,15} || (wback && n == t)"
            unpred_c = "thumb_sp_or_pc(t) || (n == t)" if wback == "TRUE" else "thumb_sp_or_pc(t)"
        ls_imm("%s_%s" % (prefix, form), instr, call + "_imm", "%s<c> <Rt>, %s" % (instr, operand), P_U_W_OPS,
               "{Rt}", "{Rn}", "{imm8}", index, add, wback, see=see, unpred=unpred, unpred_c=unpred_c)

# Unprivileged: LDRT and friends (offset form)
for instr, call in (("LDRT", "ldrt"), ("LDRBT", "ldrbt"), ("LDRHT", "ldrht"), ("LDRSBT", "ldrsbt"),
                    ("LDRSHT", "ldrsht"), ("STRT", "strt"), ("STRBT", "strbt"), ("STRHT", "strht")):
    see = ["if Rn == '1111' then SEE %s (literal);" % instr[:-1]] if instr.startswith("LD") \
        else ["if Rn == '1111' then UNDEFINED;"]
    ls_imm(instr + "_T1", instr, call, "%s<c> <Rt>, [<Rn>, #<imm8>]" % instr,
           "t = UInt(Rt); n = UInt(Rn); postindex = FALSE; add = TRUE; register_form = FALSE; imm32 = ZeroExtend(imm8, 32);",
           "{Rt}", "{Rn}", "{imm8}", see=see, unpred="t IN {13,15}", unpred_c="thumb_sp_or_pc(t)")

# Literal (32-bit)
for instr, call, id, see_pld, unpred, unpred_c in (
        ("LDR", "ldr", "LDR_l_T2", None, "t == 15 && " + IN_IT_LAST, None),
        ("LDRB", "ldrb", "LDRB_l_T1", "PLD", "t == 13", "(t == 13U)"),
        ("LDRH", "ldrh", "LDRH_l_T1", "Related encodings (memory hints)", "t == 13", "(t == 13U)"),
        ("LDRSB", "ldrsb", "LDRSB_l_T1", "PLI", "t == 13", "(t == 13U)"),
        ("LDRSH", "ldrsh", "LDRSH_l_T1", "Related encodings (memory hints)", "t == 13", "(t == 13U)")):
    see = ["if Rt == '1111' then SEE %s;" % see_pld] if see_pld else []
    ls_lit(id, instr, call + "_lit", "%s<c>.W <Rt>, <label>" % instr,
           "t = UInt(Rt); imm32 = ZeroExtend(imm12, 32); add = (U == '1');", "{Rt}", "{imm12}", "{U} != 0U",
           see=see, unpred=unpred, unpred_c=unpred_c)

# Register (32-bit): [Rn, Rm{, LSL #imm2}]
O_T2_REG = ("t = UInt(Rt); n = UInt(Rn); m = UInt(Rm); index = TRUE; add = TRUE; wback = FALSE;"
            " (shift_t, shift_n) = (SRType_LSL, UInt(imm2));")
for instr, call, see_pld, unpred, unpred_c in (
        ("LDR", "ldr", None, "m IN {13,15} || (t == 15 && %s)" % IN_IT_LAST, "thumb_sp_or_pc(m)"),
        ("LDRB", "ldrb", "PLD", "t == 13 || m IN {13,15}", "(t == 13U) || thumb_sp_or_pc(m)"),
        ("LDRH", "ldrh", "Related encodings (memory hints)", "t == 13 || m IN {13,15}", "(t == 13U) || thumb_sp_or_pc(m)"),
        ("LDRSB", "ldrsb", "PLI", "t == 13 || m IN {13,15}", "(t == 13U) || thumb_sp_or_pc(m)"),
        ("LDRSH", "ldrsh", "Related encodings (memory hints)", "t == 13 || m IN {13,15}", "(t == 13U) || thumb_sp_or_pc(m)"),
        ("STR", "str", None, "t == 15 || m IN {13,15}", "(t == 15U) || thumb_sp_or_pc(m)"),
        ("STRB", "strb", None, "t IN {13,15} || m IN {13,15}", "thumb_sp_or_pc(t) || thumb_sp_or_pc(m)"),
        ("STRH", "strh", None, "t IN {13,15} || m IN {13,15}", "thumb_sp_or_pc(t) || thumb_sp_or_pc(m)")):
    if instr.startswith("ST"):
        see = ["if Rn == '1111' then UNDEFINED;"]
    else:
        see = (["if Rt == '1111' then SEE %s;" % see_pld] if see_pld else []) + \
              ["if Rn == '1111' then SEE %s (literal);" % instr]
    ls_reg(instr + "_r_T2", instr, call + "_reg", "%s<c>.W <Rt>, [<Rn>, <Rm>{, LSL #<imm2>}]" % instr, O_T2_REG,
           "{Rt}", "{Rn}", "{Rm}", "{imm2}", see=see, unpred=unpred, unpred_c=unpred_c)

# ---------------------------------------------------------------------------
# Dual
# ---------------------------------------------------------------------------

D_OPS = ("t = UInt(Rt); t2 = UInt(Rt2); n = UInt(Rn); imm32 = ZeroExtend(imm8:'00', 32);"
         " index = (P == '1'); add = (U == '1'); wback = (W == '1');")
for instr, call in (("LDRD", "ldrd_imm"), ("STRD", "strd_imm")):
    for form, index, wback, operand in (("off", "TRUE", "FALSE", "[<Rn>{, #+/-<imm>}]"),
                                        ("pre", "TRUE", "TRUE", "[<Rn>, #+/-<imm>]!"),
                                        ("post", "FALSE", "TRUE", "[<Rn>], #+/-<imm>")):
        see = ["if P == '0' && W == '0' then SEE \"Related encodings\";"]
        if instr == "LDRD":
            see.append("if Rn == '1111' then SEE LDRD (literal);")
            unpred = "(wback && (n == t || n == t2)) || t IN {13,15} || t2 IN {13,15} || t == t2"
            unpred_c = "thumb_sp_or_pc(t) || thumb_sp_or_pc(t2) || (t == t2)"
        else:
            unpred = "(wback && (n == t || n == t2)) || n == 15 || t IN {13,15} || t2 IN {13,15}"
            unpred_c = "(n == 15U) || thumb_sp_or_pc(t) || thumb_sp_or_pc(t2)"
        if wback == "TRUE":
            unpred_c = "(n == t) || (n == t2) || " + unpred_c
        entry("%s_i_T1_%s" % (instr, form), instr, "%s<c> <Rt>, <Rt2>, %s" % (instr, operand), D_OPS, "ls_dual",
              call, [("t", "{Rt}"), ("t2", "{Rt2}"), ("n", "{Rn}")],
              {"Rt": "t", "Rt2": "t2", "Rn": "n", "imm32": "{imm8} << 2", "index": index,
               "add": "{U} != 0U", "wback": wback}, see=see, unpred=unpred, unpred_c=unpred_c)
entry("LDRD_l_T1", "LDRD", "LDRD<c> <Rt>, <Rt2>, <label>",
      "t = UInt(Rt); t2 = UInt(Rt2); imm32 = ZeroExtend(imm8:'00', 32); add = (U == '1');", "ls_dual", "ldrd_lit",
      [("t", "{Rt}"), ("t2", "{Rt2}")],
      {"Rt": "t", "Rt2": "t2", "Rn": None, "imm32": "{imm8} << 2", "index": "TRUE", "add": "{U} != 0U", "wback": "FALSE"},
      see=["if P == '0' && W == '0' then SEE \"Related encodings\";"],
      unpred="t IN {13,15} || t2 IN {13,15} || t == t2 || W == '1'",
      unpred_c="thumb_sp_or_pc(t) || thumb_sp_or_pc(t2) || (t == t2) || ({W} != 0U)")

# ---------------------------------------------------------------------------
# Multiple
# ---------------------------------------------------------------------------

# The register list is not a field of the generated accessors: bits 7:0
# (16-bit) or 12:0 (32-bit) of the encoding.
LIST16 = "(insn & 0xFFU)"
LIST32 = "(insn & 0x1FFFU)"
entry("PUSH_T1", "PUSH", "PUSH <registers>", "registers = '0':M:'000000':register_list;",
      "ls_multiple", "stmdb", [("registers", "({M} << 14) | " + LIST16)],
      {"Rn": "13U", "registers": "registers", "wback": "TRUE"}, unpred="BitCount(registers) < 1",
      unpred_c="(BitCount(registers) < 1U)")
entry("POP_T1", "POP", "POP <registers>", "registers = P:'0000000':register_list;",
      "ls_multiple", "ldm", [("registers", "({P} << 15) | " + LIST16)],
      {"Rn": "13U", "registers": "registers", "wback": "TRUE"},
      unpred="BitCount(registers) < 1 || (registers<15> == '1' && %s)" % IN_IT_LAST,
      unpred_c="(BitCount(registers) < 1U)")
entry("STM_T1", "STM", "STM<c> <Rn>!, <registers>", "n = UInt(Rn); registers = '00000000':register_list; wback = TRUE;",
      "ls_multiple", "stm", [("n", "{Rn}"), ("registers", LIST16)],
      {"Rn": "n", "registers": "registers", "wback": "TRUE"}, unpred="BitCount(registers) < 1",
      unpred_c="(BitCount(registers) < 1U)")
entry("LDM_T1", "LDM", "LDM<c> <Rn>!, <registers> / LDM<c> <Rn>, <registers> (Rn in the list)",
      "n = UInt(Rn); registers = '00000000':register_list; wback = (registers<n> == '0');",
      "ls_multiple", "ldm", [("n", "{Rn}"), ("registers", LIST16)],
      {"Rn": "n", "registers": "registers", "wback": "(ARM_BIT(registers, n) == 0U)"},
      unpred="BitCount(registers) < 1", unpred_c="(BitCount(registers) < 1U)")
M32_UNPRED_ST = "n == 15 || BitCount(registers) < 2 || (wback && registers<n> == '1')"
M32_UNPRED_ST_C = "(n == 15U) || (BitCount(registers) < 2U) || (({W} != 0U) && (ARM_BIT(registers, n) != 0U))"
M32_UNPRED_LD = ("n == 15 || BitCount(registers) < 2 || (P == '1' && M == '1') || (wback && registers<n> == '1')"
                 " || (registers<15> == '1' && %s)" % IN_IT_LAST)
M32_UNPRED_LD_C = ("(n == 15U) || (BitCount(registers) < 2U) || (({P} != 0U) && ({M} != 0U))"
                   " || (({W} != 0U) && (ARM_BIT(registers, n) != 0U))")
entry("STM_T2", "STM", "STM<c>.W <Rn>{!}, <registers>",
      "n = UInt(Rn); registers = '0':M:'0':register_list; wback = (W == '1');", "ls_multiple", "stm",
      [("n", "{Rn}"), ("registers", "({M} << 14) | " + LIST32)],
      {"Rn": "n", "registers": "registers", "wback": "{W} != 0U"}, unpred=M32_UNPRED_ST, unpred_c=M32_UNPRED_ST_C)
entry("STMDB_T1", "STMDB", "STMDB<c> <Rn>{!}, <registers>",
      "n = UInt(Rn); registers = '0':M:'0':register_list; wback = (W == '1');", "ls_multiple", "stmdb",
      [("n", "{Rn}"), ("registers", "({M} << 14) | " + LIST32)],
      {"Rn": "n", "registers": "registers", "wback": "{W} != 0U"},
      see=["if W == '1' && Rn == '1101' then SEE PUSH;"], unpred=M32_UNPRED_ST, unpred_c=M32_UNPRED_ST_C)
entry("LDM_T2", "LDM", "LDM<c>.W <Rn>{!}, <registers>",
      "n = UInt(Rn); registers = P:M:'0':register_list; wback = (W == '1');", "ls_multiple", "ldm",
      [("n", "{Rn}"), ("registers", "({P} << 15) | ({M} << 14) | " + LIST32)],
      {"Rn": "n", "registers": "registers", "wback": "{W} != 0U"},
      see=["if W == '1' && Rn == '1101' then SEE POP;"], unpred=M32_UNPRED_LD, unpred_c=M32_UNPRED_LD_C)
entry("LDMDB_T1", "LDMDB", "LDMDB<c> <Rn>{!}, <registers>",
      "n = UInt(Rn); registers = P:M:'0':register_list; wback = (W == '1');", "ls_multiple", "ldmdb",
      [("n", "{Rn}"), ("registers", "({P} << 15) | ({M} << 14) | " + LIST32)],
      {"Rn": "n", "registers": "registers", "wback": "{W} != 0U"}, unpred=M32_UNPRED_LD, unpred_c=M32_UNPRED_LD_C)

# ---------------------------------------------------------------------------
# Exclusive, table branch, hints
# ---------------------------------------------------------------------------

entry("LDREX_T1", "LDREX", "LDREX<c> <Rt>, [<Rn>{, #<imm>}]",
      "t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm8:'00', 32);", "ls_exclusive", "ldrex",
      [("t", "{Rt}"), ("n", "{Rn}")], {"Rd": None, "Rt": "t", "Rn": "n", "imm32": "{imm8} << 2"},
      unpred="t IN {13,15} || n == 15", unpred_c="thumb_sp_or_pc(t) || (n == 15U)")
for instr, call in (("LDREXB", "ldrexb"), ("LDREXH", "ldrexh")):
    entry(instr + "_T1", instr, "%s<c> <Rt>, [<Rn>]" % instr, "t = UInt(Rt); n = UInt(Rn);", "ls_exclusive", call,
          [("t", "{Rt}"), ("n", "{Rn}")], {"Rd": None, "Rt": "t", "Rn": "n", "imm32": "0U"},
          unpred="t IN {13,15} || n == 15", unpred_c="thumb_sp_or_pc(t) || (n == 15U)")
X_UNPRED = "d IN {13,15} || t IN {13,15} || n == 15 || d == n || d == t"
X_UNPRED_C = "thumb_sp_or_pc(d) || thumb_sp_or_pc(t) || (n == 15U) || (d == n) || (d == t)"
entry("STREX_T1", "STREX", "STREX<c> <Rd>, <Rt>, [<Rn>{, #<imm>}]",
      "d = UInt(Rd); t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm8:'00', 32);", "ls_exclusive", "strex",
      [("d", "{Rd}"), ("t", "{Rt}"), ("n", "{Rn}")], {"Rd": "d", "Rt": "t", "Rn": "n", "imm32": "{imm8} << 2"},
      unpred=X_UNPRED, unpred_c=X_UNPRED_C)
for instr, call in (("STREXB", "strexb"), ("STREXH", "strexh")):
    entry(instr + "_T1", instr, "%s<c> <Rd>, <Rt>, [<Rn>]" % instr, "d = UInt(Rd); t = UInt(Rt); n = UInt(Rn);",
          "ls_exclusive", call, [("d", "{Rd}"), ("t", "{Rt}"), ("n", "{Rn}")],
          {"Rd": "d", "Rt": "t", "Rn": "n", "imm32": "0U"}, unpred=X_UNPRED, unpred_c=X_UNPRED_C)
entry("CLREX_T1", "CLREX", "CLREX<c>", "// No additional decoding required", "clrex", "clrex", [], {})
for instr, call in (("TBB", "tbb"), ("TBH", "tbh")):
    entry(instr + "_T1", instr, "%s<c> [<Rn>, <Rm>%s]" % (instr, ", LSL #1" if instr == "TBH" else ""),
          "n = UInt(Rn); m = UInt(Rm); is_tbh = %s;" % ("TRUE" if instr == "TBH" else "FALSE"), "tb", call,
          [("n", "{Rn}"), ("m", "{Rm}")], {"Rn": "n", "Rm": "m"},
          unpred="n == 13 || m IN {13,15} || (%s)" % IN_IT_LAST, unpred_c="(n == 13U) || thumb_sp_or_pc(m)")
# Memory hints: no architectural effect in this target.
for id, instr, syntax, ops, unpred, unpred_c, regs in (
        ("PLD_i_T1", "PLD", "PLD<c> [<Rn>, #<imm12>]", "n = UInt(Rn); imm32 = ZeroExtend(imm12, 32); add = TRUE;", None, None, []),
        ("PLD_i_T2", "PLD", "PLD<c> [<Rn>, #-<imm8>]", "n = UInt(Rn); imm32 = ZeroExtend(imm8, 32); add = FALSE;", None, None, []),
        ("PLD_l_T1", "PLD", "PLD<c> <label>", "imm32 = ZeroExtend(imm12, 32); add = (U == '1');", None, None, []),
        ("PLD_r_T1", "PLD", "PLD<c> [<Rn>, <Rm>{, LSL #<imm2>}]",
         "n = UInt(Rn); m = UInt(Rm); add = TRUE; (shift_t, shift_n) = (SRType_LSL, UInt(imm2));",
         "m IN {13,15}", "thumb_sp_or_pc(m)", [("m", "{Rm}")]),
        ("PLI_i_T1", "PLI", "PLI<c> [<Rn>, #<imm12>]", "n = UInt(Rn); imm32 = ZeroExtend(imm12, 32); add = TRUE;", None, None, []),
        ("PLI_i_T2", "PLI", "PLI<c> [<Rn>, #-<imm8>]", "n = UInt(Rn); imm32 = ZeroExtend(imm8, 32); add = FALSE;", None, None, []),
        ("PLI_i_T3", "PLI", "PLI<c> <label>", "n = 15; imm32 = ZeroExtend(imm12, 32); add = (U == '1');", None, None, []),
        ("PLI_r_T1", "PLI", "PLI<c> [<Rn>, <Rm>{, LSL #<imm2>}]",
         "n = UInt(Rn); m = UInt(Rm); add = TRUE; (shift_t, shift_n) = (SRType_LSL, UInt(imm2));",
         "m IN {13,15}", "thumb_sp_or_pc(m)", [("m", "{Rm}")])):
    entry(id, instr, syntax, ops, "hint", None, regs, {}, unpred=unpred, unpred_c=unpred_c,
          note="A memory hint: no effect here (no caches modelled).")


# ---------------------------------------------------------------------------
# Branches with link, compare and branch
# ---------------------------------------------------------------------------

entry("BL_i_T1", "BL", "BL<c> <label>",
      "I1 = NOT(J1 EOR S); I2 = NOT(J2 EOR S); imm32 = SignExtend(S:I1:I2:imm10:imm11:'0', 32);", "bl", "bl",
      [("s", "{S}"), ("i1", "({J1} ^ s) ^ 1U"), ("i2", "({J2} ^ s) ^ 1U")],
      {"imm32": "SignExtend((s << 24) | (i1 << 23) | (i2 << 22) | ({imm10} << 12) | ({imm11} << 1), 25U)"},
      unpred=IN_IT_LAST)
entry("BLX_r_T1", "BLX", "BLX<c> <Rm>", "m = UInt(Rm);", "blx", "blx_reg", [("m", "{Rm}")], {"Rm": "m"},
      unpred="m == 15 || (%s)" % IN_IT_LAST, unpred_c="m == 15U")
for instr, nonzero in (("CBZ", "FALSE"), ("CBNZ", "TRUE")):
    entry(instr + "_T1", instr, "%s <Rn>, <label>" % instr,
          "n = UInt(Rn); imm32 = ZeroExtend(i:imm5:'0', 32); nonzero = (op == '1');", "cb", "cb",
          [("n", "{Rn}")], {"Rn": "n", "imm32": "({i} << 6) | ({imm5} << 1)", "nonzero": nonzero},
          unpred="InITBlock()")

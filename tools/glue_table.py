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
IN_IT_LAST_C = "thumb_in_it_not_last(core)"


class Entry:
    def __init__(self, id, instr, syntax, ops, kind, call, regs, members,
                 see=(), unpred=None, unpred_c=None, note=None, later_c=None, undef=None, undef_c=None):
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
        self.later_c = later_c    # C condition of forms not executed yet (STD_E_DECODE)
        self.undef = undef        # the manual's UNDEFINED condition, and its C (STD_E_DECODE)
        self.undef_c = undef_c


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
    "t == 15 && " + IN_IT_LAST: "(t == 15U) && " + IN_IT_LAST_C,
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
            unpred_c = ("(n == t) || " if wback == "TRUE" else "") + "((t == 15U) && %s)" % IN_IT_LAST_C
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
        ("LDR", "ldr", "LDR_l_T2", None, "t == 15 && " + IN_IT_LAST, "(t == 15U) && " + IN_IT_LAST_C),
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
        ("LDR", "ldr", None, "m IN {13,15} || (t == 15 && %s)" % IN_IT_LAST,
         "thumb_sp_or_pc(m) || ((t == 15U) && %s)" % IN_IT_LAST_C),
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
      unpred_c="(BitCount(registers) < 1U) || ((ARM_BIT(registers, 15U) != 0U) && %s)" % IN_IT_LAST_C)
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
                   " || (({W} != 0U) && (ARM_BIT(registers, n) != 0U))"
                   " || ((ARM_BIT(registers, 15U) != 0U) && %s)" % IN_IT_LAST_C)
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
          unpred="n == 13 || m IN {13,15} || (%s)" % IN_IT_LAST, unpred_c="(n == 13U) || thumb_sp_or_pc(m) || " + IN_IT_LAST_C)
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
      unpred=IN_IT_LAST, unpred_c=IN_IT_LAST_C)
entry("BLX_r_T1", "BLX", "BLX<c> <Rm>", "m = UInt(Rm);", "blx", "blx_reg", [("m", "{Rm}")], {"Rm": "m"},
      unpred="m == 15 || (%s)" % IN_IT_LAST, unpred_c="(m == 15U) || " + IN_IT_LAST_C)
for instr, nonzero in (("CBZ", "FALSE"), ("CBNZ", "TRUE")):
    entry(instr + "_T1", instr, "%s <Rn>, <label>" % instr,
          "n = UInt(Rn); imm32 = ZeroExtend(i:imm5:'0', 32); nonzero = (op == '1');", "cb", "cb",
          [("n", "{Rn}")], {"Rn": "n", "imm32": "({i} << 6) | ({imm5} << 1)", "nonzero": nonzero},
          unpred="InITBlock()", unpred_c="Stm32InITBlock(core)")


# ---------------------------------------------------------------------------
# SP-relative ADD and SUB, ADR, MOVT
# ---------------------------------------------------------------------------

def dp_imm(id, instr, call, syntax, ops, d, n, imm32, setflags, **kw):
    entry(id, instr, syntax, ops, "dp_imm", call, [("d", d)],
          {"setflags": setflags, "Rd": "d", "Rn": n, "imm32": imm32, "carry": "FALSE"}, **kw)


def dp_reg(id, instr, call, syntax, ops, d, n, m, setflags, shift_t, shift_n, more_regs=(), **kw):
    entry(id, instr, syntax, ops, "dp_reg", call, [("d", d), ("m", m)] + list(more_regs),
          {"setflags": setflags, "Rd": "d", "Rn": n, "Rm": "m", "shift_t": shift_t, "shift_n": shift_n}, **kw)


dp_reg("ADD_SP_r_T1", "ADD", "add_reg", "ADD<c> <Rdm>, SP, <Rdm>",
       "d = UInt(DM:Rdm); m = UInt(DM:Rdm); setflags = FALSE; (shift_t, shift_n) = (SRType_LSL, 0);",
       "({DM} << 3) | {Rdm}", "13U", "d", "FALSE", "SRType_LSL", "0U",
       unpred="d == 15 && " + IN_IT_LAST, unpred_c="(d == 15U) && " + IN_IT_LAST_C)
dp_reg("ADD_SP_r_T2", "ADD", "add_reg", "ADD<c> SP, <Rm>",
       "if Rm == '1101' then SEE encoding T1; d = 13; m = UInt(Rm); setflags = FALSE;"
       " (shift_t, shift_n) = (SRType_LSL, 0);",
       "13U", "13U", "{Rm}", "FALSE", "SRType_LSL", "0U",
       note="(Rm == '1101' is ADD SP, SP, SP either way.)")
dp_imm("ADD_SP_i_T1", "ADD", "add_imm", "ADD<c> <Rd>, SP, #<imm32>",
       "d = UInt(Rd); setflags = FALSE; imm32 = ZeroExtend(imm8:'00', 32);", "{Rd}", "13U", "{imm8} << 2", "FALSE")
dp_imm("ADD_SP_i_T2", "ADD", "add_imm", "ADD<c> SP, SP, #<imm32>",
       "d = 13; setflags = FALSE; imm32 = ZeroExtend(imm7:'00', 32);", "13U", "13U", "{imm7} << 2", "FALSE")
dp_imm("SUB_SP_i_T1", "SUB", "sub_imm", "SUB<c> SP, SP, #<imm32>",
       "d = 13; setflags = FALSE; imm32 = ZeroExtend(imm7:'00', 32);", "13U", "13U", "{imm7} << 2", "FALSE")
for instr, call, idt in (("ADD", "add_reg", "ADD%s_SP_r_T3"), ("SUB", "sub_reg", "SUB%s_SP_r_T1")):
    for s_ in ("", "S"):
        for rrx in (False, True):
            id = (idt % s_) + ("_RRX" if rrx else "")
            flags = "TRUE" if s_ else "FALSE"
            if rrx:
                shift_t, shift_n = "SRType_RRX", "1U"
                sp_c = "d == 13U"
                more = []
            else:
                shift_t = "thumb_shift_t(type, imm5)"
                shift_n = "thumb_shift_n(type, imm5)"
                sp_c = "(d == 13U) && ((type != 0U) || (imm5 > 3U))"
                more = [("type", "{stype}"), ("imm5", "thumb_imm5({imm3}, {imm2})")]
            dp_reg(id, instr, call, "%s%s<c>.W <Rd>, SP, <Rm>{, <shift>}" % (instr, s_),
                   "d = UInt(Rd); m = UInt(Rm); setflags = (S == '1');"
                   " (shift_t, shift_n) = DecodeImmShift(type, imm3:imm2);",
                   "{Rd}", "13U", "{Rm}", flags, shift_t, shift_n, more_regs=more,
                   unpred="d == 13 && (shift_t != SRType_LSL || shift_n > 3); (d == 15 && !setflags) || m IN {13,15}",
                   unpred_c="(%s) || %s(m == 13U) || (m == 15U)" % (sp_c, "" if s_ else "(d == 15U) || "))
for instr, call, idt in (("ADD", "add_imm", "ADD%s_SP_i_T3"), ("SUB", "sub_imm", "SUB%s_SP_i_T2")):
    for s_ in ("", "S"):
        dp_imm(idt % s_, instr, call, "%s%s<c>.W <Rd>, SP, #<const>" % (instr, s_),
               "d = UInt(Rd); setflags = (S == '1'); imm32 = ThumbExpandImm(i:imm3:imm8);",
               "{Rd}", "13U", "ThumbExpandImm(thumb_imm12({i}, {imm3}, {imm8}))", "TRUE" if s_ else "FALSE",
               unpred="d == 15 && !setflags", unpred_c=None if s_ else "d == 15U",
               note="(ADDS/SUBS with Rd == '1111' is CMN/CMP: another id in the generated decoder.)" if s_ else None)
for id, instr, call in (("ADD_SP_i_T4", "ADDW", "add_imm"), ("SUB_SP_i_T3", "SUBW", "sub_imm")):
    dp_imm(id, instr, call, "%s<c> <Rd>, SP, #<imm12>" % instr,
           "d = UInt(Rd); setflags = FALSE; imm32 = ZeroExtend(i:imm3:imm8, 32);",
           "{Rd}", "13U", "thumb_imm12({i}, {imm3}, {imm8})", "FALSE", unpred="d == 15", unpred_c="d == 15U")
entry("ADR_T1", "ADR", "ADR<c> <Rd>, <label>", "d = UInt(Rd); imm32 = ZeroExtend(imm8:'00', 32); add = TRUE;",
      "adr", "adr", [("d", "{Rd}")], {"Rd": "d", "imm32": "{imm8} << 2", "add": "TRUE"})
for id, add in (("ADR_T2", "FALSE"), ("ADR_T3", "TRUE")):
    entry(id, "ADR", "ADR<c>.W <Rd>, <label>",
          "d = UInt(Rd); imm32 = ZeroExtend(i:imm3:imm8, 32); add = %s;" % add,
          "adr", "adr", [("d", "{Rd}")], {"Rd": "d", "imm32": "thumb_imm12({i}, {imm3}, {imm8})", "add": add},
          unpred="d IN {13,15}", unpred_c="thumb_sp_or_pc(d)")
entry("MOVT_T1", "MOVT", "MOVT<c> <Rd>, #<imm16>", "d = UInt(Rd); imm16 = imm4:i:imm3:imm8;", "dp_imm", "movt",
      [("d", "{Rd}")], {"setflags": "FALSE", "Rd": "d", "Rn": None,
                        "imm32": "({imm4} << 12) | ({i} << 11) | ({imm3} << 8) | {imm8}", "carry": "FALSE"},
      unpred="d IN {13,15}", unpred_c="thumb_sp_or_pc(d)")

# ---------------------------------------------------------------------------
# Extend, bitfield, bit operations
# ---------------------------------------------------------------------------

for instr in ("SXTB", "SXTH", "UXTB", "UXTH"):
    entry(instr + "_T1", instr, "%s<c> <Rd>, <Rm>" % instr, "d = UInt(Rd); m = UInt(Rm); rotation = 0;",
          "extend", instr.lower(), [("d", "{Rd}"), ("m", "{Rm}")], {"Rd": "d", "Rn": None, "Rm": "m", "rotation": "0U"})
for instr in ("SXTB", "SXTH", "UXTB", "UXTH", "SXTB16", "UXTB16"):
    id = instr + ("_T1" if instr.endswith("16") else "_T2")
    entry(id, instr, "%s<c>%s <Rd>, <Rm>{, <rotation>}" % (instr, "" if instr.endswith("16") else ".W"),
          "d = UInt(Rd); m = UInt(Rm); rotation = UInt(rotate:'000');",
          "extend", instr.lower(), [("d", "{Rd}"), ("m", "{Rm}")],
          {"Rd": "d", "Rn": None, "Rm": "m", "rotation": "{rotate} << 3"},
          unpred="d IN {13,15} || m IN {13,15}", unpred_c="thumb_sp_or_pc(d) || thumb_sp_or_pc(m)")
for instr in ("SXTAB", "SXTAH", "UXTAB", "UXTAH", "SXTAB16", "UXTAB16"):
    entry(instr + "_T1", instr, "%s<c> <Rd>, <Rn>, <Rm>{, <rotation>}" % instr,
          "if Rn == '1111' then SEE %s; d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); rotation = UInt(rotate:'000');"
          % instr.replace("A", "", 1),
          "extend", instr.lower(), [("d", "{Rd}"), ("n", "{Rn}"), ("m", "{Rm}")],
          {"Rd": "d", "Rn": "n", "Rm": "m", "rotation": "{rotate} << 3"},
          unpred="d IN {13,15} || n == 13 || m IN {13,15}",
          unpred_c="thumb_sp_or_pc(d) || (n == 13U) || thumb_sp_or_pc(m)")
for instr in ("SBFX", "UBFX"):
    entry(instr + "_T1", instr, "%s<c> <Rd>, <Rn>, #<lsb>, #<width>" % instr,
          "d = UInt(Rd); n = UInt(Rn); lsbit = UInt(imm3:imm2); widthminus1 = UInt(widthm1);",
          "bfx", instr.lower(), [("d", "{Rd}"), ("n", "{Rn}")],
          {"Rd": "d", "Rn": "n", "lsbit": "thumb_imm5({imm3}, {imm2})", "widthminus1": "{widthm1}"},
          unpred="d IN {13,15} || n IN {13,15}", unpred_c="thumb_sp_or_pc(d) || thumb_sp_or_pc(n)")
entry("BFI_T1", "BFI", "BFI<c> <Rd>, <Rn>, #<lsb>, #<width>",
      "if Rn == '1111' then SEE BFC; d = UInt(Rd); n = UInt(Rn); msbit = UInt(msb); lsbit = UInt(imm3:imm2);",
      "bfi", "bfi", [("d", "{Rd}"), ("n", "{Rn}")],
      {"Rd": "d", "Rn": "n", "lsbit": "thumb_imm5({imm3}, {imm2})", "msbit": "{msb}"},
      unpred="d IN {13,15} || n == 13", unpred_c="thumb_sp_or_pc(d) || (n == 13U)")
entry("BFC_T1", "BFC", "BFC<c> <Rd>, #<lsb>, #<width>",
      "d = UInt(Rd); msbit = UInt(msb); lsbit = UInt(imm3:imm2);",
      "bfi", "bfc", [("d", "{Rd}")],
      {"Rd": "d", "Rn": None, "lsbit": "thumb_imm5({imm3}, {imm2})", "msbit": "{msb}"},
      unpred="d IN {13,15}", unpred_c="thumb_sp_or_pc(d)")
for instr in ("REV", "REV16", "REVSH"):
    entry(instr + "_T1", instr, "%s<c> <Rd>, <Rm>" % instr, "d = UInt(Rd); m = UInt(Rm);",
          "unary", instr.lower(), [("d", "{Rd}"), ("m", "{Rm}")], {"Rd": "d", "Rm": "m"})
for instr, id in (("REV", "REV_T2"), ("REV16", "REV16_T2"), ("REVSH", "REVSH_T2"), ("RBIT", "RBIT_T1"), ("CLZ", "CLZ_T1")):
    entry(id, instr, "%s<c>%s <Rd>, <Rm>" % (instr, ".W" if instr.startswith("REV") else ""),
          "if !Consistent(Rm) then UNPREDICTABLE; d = UInt(Rd); m = UInt(Rm);",
          "unary", instr.lower(), [("d", "{Rd}"), ("m", "{Rm}")], {"Rd": "d", "Rm": "m"},
          unpred="d IN {13,15} || m IN {13,15}",
          unpred_c="({Rn} != m) || thumb_sp_or_pc(d) || thumb_sp_or_pc(m)",
          note="(Consistent(Rm): the second copy of Rm, the decoder's Rn field, is the same.)")

# ---------------------------------------------------------------------------
# Multiply and divide
# ---------------------------------------------------------------------------

MUL_UNPRED = "d IN {13,15} || n IN {13,15} || m IN {13,15}"
MUL_UNPRED_C = "thumb_sp_or_pc(d) || thumb_sp_or_pc(n) || thumb_sp_or_pc(m)"
entry("MUL_T1", "MUL", "MULS <Rdm>, <Rn>, <Rdm>", "d = UInt(Rdm); n = UInt(Rn); m = UInt(Rdm); setflags = !InITBlock();",
      "mul", "mul", [("d", "{Rdm}"), ("n", "{Rn}")],
      {"setflags": "!Stm32InITBlock(core)", "Rd": "d", "Rn": "n", "Rm": "d", "Ra": None})
entry("MUL_T2", "MUL", "MUL<c> <Rd>, <Rn>, <Rm>", "d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); setflags = FALSE;",
      "mul", "mul", [("d", "{Rd}"), ("n", "{Rn}"), ("m", "{Rm}")],
      {"setflags": "FALSE", "Rd": "d", "Rn": "n", "Rm": "m", "Ra": None}, unpred=MUL_UNPRED, unpred_c=MUL_UNPRED_C)
entry("MLA_T1", "MLA", "MLA<c> <Rd>, <Rn>, <Rm>, <Ra>",
      "if Ra == '1111' then SEE MUL; d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); a = UInt(Ra); setflags = FALSE;",
      "mul", "mla", [("d", "{Rd}"), ("n", "{Rn}"), ("m", "{Rm}"), ("a", "{Ra}")],
      {"setflags": "FALSE", "Rd": "d", "Rn": "n", "Rm": "m", "Ra": "a"},
      unpred=MUL_UNPRED + " || a == 13", unpred_c=MUL_UNPRED_C + " || (a == 13U)")
entry("MLS_T1", "MLS", "MLS<c> <Rd>, <Rn>, <Rm>, <Ra>",
      "d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); a = UInt(Ra);",
      "mul", "mls", [("d", "{Rd}"), ("n", "{Rn}"), ("m", "{Rm}"), ("a", "{Ra}")],
      {"setflags": "FALSE", "Rd": "d", "Rn": "n", "Rm": "m", "Ra": "a"},
      unpred=MUL_UNPRED + " || a IN {13,15}", unpred_c=MUL_UNPRED_C + " || thumb_sp_or_pc(a)")
for instr in ("SDIV", "UDIV"):
    entry(instr + "_T1", instr, "%s<c> <Rd>, <Rn>, <Rm>" % instr, "d = UInt(Rd); n = UInt(Rn); m = UInt(Rm);",
          "mul", instr.lower(), [("d", "{Rd}"), ("n", "{Rn}"), ("m", "{Rm}")],
          {"setflags": "FALSE", "Rd": "d", "Rn": "n", "Rm": "m", "Ra": None}, unpred=MUL_UNPRED, unpred_c=MUL_UNPRED_C)
for instr in ("SMULL", "UMULL", "SMLAL", "UMLAL"):
    entry(instr + "_T1", instr, "%s<c> <RdLo>, <RdHi>, <Rn>, <Rm>" % instr,
          "dLo = UInt(RdLo); dHi = UInt(RdHi); n = UInt(Rn); m = UInt(Rm); setflags = FALSE;",
          "mul_long", instr.lower(), [("dlo", "{RdLo}"), ("dhi", "{RdHi}"), ("n", "{Rn}"), ("m", "{Rm}")],
          {"setflags": "FALSE", "RdLo": "dlo", "RdHi": "dhi", "Rn": "n", "Rm": "m"},
          unpred="dLo IN {13,15} || dHi IN {13,15} || n IN {13,15} || m IN {13,15}; dHi == dLo",
          unpred_c="thumb_sp_or_pc(dlo) || thumb_sp_or_pc(dhi) || thumb_sp_or_pc(n) || thumb_sp_or_pc(m) || (dhi == dlo)")


# ---------------------------------------------------------------------------
# BX, IT, MRS and MSR (program status registers), barriers and hints
# ---------------------------------------------------------------------------

entry("BX_T1", "BX", "BX<c> <Rm>", "m = UInt(Rm);", "bx", "bx", [("m", "{Rm}")], {"Rm": "m"},
      unpred=IN_IT_LAST, unpred_c=IN_IT_LAST_C)
entry("IT_T1", "IT", "IT{<x>{<y>{<z>}}} <firstcond>",
      "if mask == '0000' then SEE \"Related encodings\";", "it", "it",
      [("firstcond", "{firstcond}"), ("mask", "{mask}")], {"firstcond": "firstcond", "mask": "mask"},
      unpred="firstcond == '1111' || (firstcond == '1110' && BitCount(mask) != 1); InITBlock()",
      unpred_c="(firstcond == 15U) || ((firstcond == 14U) && (BitCount(mask) != 1U)) || Stm32InITBlock(core)",
      note="(mask == '0000' is the hints: other ids in the generated decoder.)")
# The generated decoder has the Armv7-A forms of MRS and MSR (fields R, mask<3:0>); in
# Armv7-M, SYSm is bits 7:0 and mask<1:0> bits 11:10, and R and mask<1:0> of the
# A form are (0).
SYSM = "(insn & 0xFFU)"
entry("MRS_T1_AS", "MRS", "MRS<c> <Rd>, <spec_reg>",
      "d = UInt(Rd);",
      "mrs", "mrs_xpsr", [("d", "{Rd}"), ("sysm", SYSM)], {"Rd": "d", "SYSm": "sysm"},
      unpred="d IN {13,15} || !(UInt(SYSm) IN {0..3,5..9,16..20})",
      unpred_c="thumb_sp_or_pc(d) || !thumb_sysm_valid(sysm) || ({R} != 0U)",
      note="(R, (0) in Armv7-M, set: UNPREDICTABLE here. SYSm 8 and above (MSP, PSP, PRIMASK, BASEPRI,"
           " FAULTMASK, CONTROL) are not executed yet.)",
      later_c="sysm >= 8U")
entry("MSR_r_T1_AS", "MSR", "MSR<c> <spec_reg>, <Rn>",
      "n = UInt(Rn);",
      "msr", "msr_xpsr", [("n", "{Rn}"), ("sysm", SYSM), ("mask", "{mask} >> 2")],
      {"Rn": "n", "SYSm": "sysm", "mask": "mask"},
      unpred="mask == '00' || (mask != '10' && !(UInt(SYSm) IN {0..3})); n IN {13,15} || !(UInt(SYSm) IN {0..3,5..9,16..20})",
      unpred_c="(mask == 0U) || ((mask != 2U) && (sysm > 3U)) || thumb_sp_or_pc(n) || !thumb_sysm_valid(sysm)"
               " || ({R} != 0U) || (({mask} & 3U) != 0U)",
      note="(R and bits 9:8, (0) in Armv7-M, set: UNPREDICTABLE here. SYSm 8 and above are not executed yet.)",
      later_c="sysm >= 8U")
for id, instr, syntax, note in (
        ("DMB_T1", "DMB", "DMB<c> <option>", "A barrier: no effect here (one core; memory accesses complete in order)."),
        ("DSB_T1", "DSB", "DSB<c> <option>", "A barrier: no effect here (one core; memory accesses complete in order)."),
        ("ISB_T1", "ISB", "ISB<c> <option>", "A barrier: no effect here (no pipeline: each instruction is fetched as it runs)."),
        ("NOP_T2", "NOP", "NOP<c>.W", "No operation."),
        ("YIELD_T1", "YIELD", "YIELD<c>", "A hint: no effect here (no other threads)."),
        ("YIELD_T2", "YIELD", "YIELD<c>.W", "A hint: no effect here (no other threads)."),
        ("DBG_T1", "DBG", "DBG<c> #<option>", "A hint: no effect here (no debug system).")):
    entry(id, instr, syntax, "// No additional decoding required", "hint", None, [], {}, note=note)


# ---------------------------------------------------------------------------
# DSP extension: parallel addition and subtraction, SEL, saturation, PKH,
# DSP multiplies
# ---------------------------------------------------------------------------

DSP_UNPRED = "d IN {13,15} || n IN {13,15} || m IN {13,15}"
DSP_UNPRED_C = "thumb_sp_or_pc(d) || thumb_sp_or_pc(n) || thumb_sp_or_pc(m)"
DSP_REGS = [("d", "{Rd}"), ("n", "{Rn}"), ("m", "{Rm}")]


def dsp(id, instr, call, syntax, ops, ra=None, n_high="FALSE", m_high="FALSE", m_swap="FALSE", round_="FALSE", **kw):
    """ra: None (no Ra), "acc" (a == 13 UNPREDICTABLE) or "acc_sp_pc" (a IN {13,15})."""
    regs = list(DSP_REGS)
    unpred, unpred_c = DSP_UNPRED, DSP_UNPRED_C
    if ra:
        regs.append(("a", "{Ra}"))
        unpred += " || a == 13" if ra == "acc" else " || a IN {13,15}"
        unpred_c += " || (a == 13U)" if ra == "acc" else " || thumb_sp_or_pc(a)"
    entry(id, instr, syntax, ops, "dsp", call, regs,
          {"Rd": "d", "Rn": "n", "Rm": "m", "Ra": "a" if ra else None,
           "n_high": n_high, "m_high": m_high, "m_swap": m_swap, "round": round_},
          unpred=unpred, unpred_c=unpred_c, **kw)


for op in ("ADD8", "ADD16", "ASX", "SUB8", "SUB16", "SAX"):
    for prefix in ("S", "Q", "SH", "U", "UQ", "UH"):
        instr = prefix + op
        dsp(instr + "_T1", instr, instr.lower(), "%s<c> <Rd>, <Rn>, <Rm>" % instr, "d = UInt(Rd); n = UInt(Rn); m = UInt(Rm);")
for instr in ("QADD", "QSUB", "QDADD", "QDSUB", "SEL"):
    dsp(instr + "_T1", instr, instr.lower(), "%s<c> <Rd>, <Rn>, <Rm>" % instr if instr == "SEL"
        else "%s<c> <Rd>, <Rm>, <Rn>" % instr, "d = UInt(Rd); n = UInt(Rn); m = UInt(Rm);")
for x in "BT":
    for y in "BT":
        nh, mh = ("TRUE" if x == "T" else "FALSE"), ("TRUE" if y == "T" else "FALSE")
        hl = "n_high = (N == '1'); m_high = (M == '1');"
        dsp("SMUL%s%s_T1" % (x, y), "SMUL%s%s" % (x, y), "smulxy", "SMUL%s%s<c> <Rd>, <Rn>, <Rm>" % (x, y),
            "d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); " + hl, n_high=nh, m_high=mh)
        dsp("SMLA%s%s_T1" % (x, y), "SMLA%s%s" % (x, y), "smlaxy", "SMLA%s%s<c> <Rd>, <Rn>, <Rm>, <Ra>" % (x, y),
            "if Ra == '1111' then SEE SMULBB, SMULBT, SMULTB, SMULTT;"
            " d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); a = UInt(Ra); " + hl, ra="acc", n_high=nh, m_high=mh)
for y in "BT":
    mh = "TRUE" if y == "T" else "FALSE"
    dsp("SMULW%s_T1" % y, "SMULW" + y, "smulwy", "SMULW%s<c> <Rd>, <Rn>, <Rm>" % y,
        "d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); m_high = (M == '1');", m_high=mh)
    dsp("SMLAW%s_T1" % y, "SMLAW" + y, "smlawy", "SMLAW%s<c> <Rd>, <Rn>, <Rm>, <Ra>" % y,
        "if Ra == '1111' then SEE SMULWB, SMULWT; d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); a = UInt(Ra);"
        " m_high = (M == '1');", ra="acc", m_high=mh)
for base, acc, see in (("SMUAD", None, None), ("SMLAD", "acc", "SMUAD"), ("SMUSD", None, None), ("SMLSD", "acc", "SMUSD")):
    for x in ("", "X"):
        instr = base + x
        ops = ("if Ra == '1111' then SEE %s; " % see if see else "") + \
            "d = UInt(Rd); n = UInt(Rn); m = UInt(Rm);%s m_swap = (M == '1');" % (" a = UInt(Ra);" if acc else "")
        dsp(instr + "_T1", instr, base.lower(), "%s<c> <Rd>, <Rn>, <Rm>%s" % (instr, ", <Ra>" if acc else ""), ops,
            ra=acc, m_swap="TRUE" if x else "FALSE")
for base, acc, see in (("SMMUL", None, None), ("SMMLA", "acc", "SMMUL"), ("SMMLS", "acc_sp_pc", None)):
    for r in ("", "R"):
        instr = base + r
        ops = ("if Ra == '1111' then SEE %s; " % see if see else "") + \
            "d = UInt(Rd); n = UInt(Rn); m = UInt(Rm);%s round = (R == '1');" % (" a = UInt(Ra);" if acc else "")
        dsp(instr + "_T1", instr, base.lower(), "%s<c> <Rd>, <Rn>, <Rm>%s" % (instr, ", <Ra>" if acc else ""), ops,
            ra=acc, round_="TRUE" if r else "FALSE")
dsp("USAD8_T1", "USAD8", "usad8", "USAD8<c> <Rd>, <Rn>, <Rm>", "d = UInt(Rd); n = UInt(Rn); m = UInt(Rm);")
dsp("USADA8_T1", "USADA8", "usada8", "USADA8<c> <Rd>, <Rn>, <Rm>, <Ra>",
    "if Ra == '1111' then SEE USAD8; d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); a = UInt(Ra);", ra="acc")


def dsp_long(id, instr, call, syntax, ops, n_high="FALSE", m_high="FALSE", m_swap="FALSE"):
    entry(id, instr, syntax, ops, "dsp_long", call, [("dlo", "{RdLo}"), ("dhi", "{RdHi}"), ("n", "{Rn}"), ("m", "{Rm}")],
          {"RdLo": "dlo", "RdHi": "dhi", "Rn": "n", "Rm": "m", "n_high": n_high, "m_high": m_high, "m_swap": m_swap},
          unpred="dLo IN {13,15} || dHi IN {13,15} || n IN {13,15} || m IN {13,15}; dHi == dLo",
          unpred_c="thumb_sp_or_pc(dlo) || thumb_sp_or_pc(dhi) || thumb_sp_or_pc(n) || thumb_sp_or_pc(m) || (dhi == dlo)")


L_OPS = "dLo = UInt(RdLo); dHi = UInt(RdHi); n = UInt(Rn); m = UInt(Rm);"
for x in "BT":
    for y in "BT":
        dsp_long("SMLAL%s%s_T1" % (x, y), "SMLAL%s%s" % (x, y), "smlalxy", "SMLAL%s%s<c> <RdLo>, <RdHi>, <Rn>, <Rm>" % (x, y),
                 L_OPS + " n_high = (N == '1'); m_high = (M == '1');",
                 n_high="TRUE" if x == "T" else "FALSE", m_high="TRUE" if y == "T" else "FALSE")
for base in ("SMLALD", "SMLSLD"):
    for x in ("", "X"):
        dsp_long(base + x + "_T1", base + x, base.lower(), "%s%s<c> <RdLo>, <RdHi>, <Rn>, <Rm>" % (base, x),
                 L_OPS + " m_swap = (M == '1');", m_swap="TRUE" if x else "FALSE")
dsp_long("UMAAL_T1", "UMAAL", "umaal", "UMAAL<c> <RdLo>, <RdHi>, <Rn>, <Rm>", L_OPS)

# Saturation: SSAT and USAT have one id per shift (sh: LSL, ASR)
SAT_UNPRED, SAT_UNPRED_C = "d IN {13,15} || n IN {13,15}", "thumb_sp_or_pc(d) || thumb_sp_or_pc(n)"
for instr, plus in (("SSAT", " + 1U"), ("USAT", "")):
    for sh, shift_t in (("LSL", "SRType_LSL"), ("ASR", "SRType_ASR")):
        entry("%s_T1_%s" % (instr, sh), instr, "%s<c> <Rd>, #<imm>, <Rn>{, <shift>}" % instr,
              "if sh == '1' && (imm3:imm2) == '00000' then SEE %s16; d = UInt(Rd); n = UInt(Rn);"
              " saturate_to = UInt(sat_imm)%s; (shift_t, shift_n) = DecodeImmShift(sh:'0', imm3:imm2);"
              % (instr, "+1" if plus else ""), "sat", instr.lower(), [("d", "{Rd}"), ("n", "{Rn}")],
              {"Rd": "d", "Rn": "n", "saturate_to": "{sat_imm}" + plus, "shift_t": shift_t,
               "shift_n": "thumb_imm5({imm3}, {imm2})"},
              unpred=SAT_UNPRED, unpred_c=SAT_UNPRED_C,
              note="(sh = '1' with a zero amount is %s16: another id in the generated decoder.)" % instr
              if sh == "ASR" else None)
    entry(instr + "16_T1", instr + "16", "%s16<c> <Rd>, #<imm>, <Rn>" % instr,
          "d = UInt(Rd); n = UInt(Rn); saturate_to = UInt(sat_imm)%s;" % ("+1" if plus else ""),
          "sat", instr.lower() + "16", [("d", "{Rd}"), ("n", "{Rn}")],
          {"Rd": "d", "Rn": "n", "saturate_to": "{sat_imm}" + plus, "shift_t": "SRType_LSL", "shift_n": "0U"},
          unpred=SAT_UNPRED, unpred_c=SAT_UNPRED_C)
for instr, shift_t in (("PKHBT", "SRType_LSL"), ("PKHTB", "SRType_ASR")):
    tb = instr == "PKHTB"
    entry(instr + "_T1", instr, "%s<c> <Rd>, <Rn>, <Rm>{, %s #<imm>}" % (instr, "ASR" if tb else "LSL"),
          "d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); tbform = (tb == '1');"
          " (shift_t, shift_n) = DecodeImmShift(tb:'0', imm3:imm2);", "dp_reg", instr.lower(), DSP_REGS,
          {"setflags": "FALSE", "Rd": "d", "Rn": "n", "Rm": "m", "shift_t": shift_t,
           "shift_n": "thumb_shift_n(%s, thumb_imm5({imm3}, {imm2}))" % ("2U" if tb else "0U")},
          unpred=DSP_UNPRED, unpred_c=DSP_UNPRED_C)


# ---------------------------------------------------------------------------
# Floating point (FPv5-D16): moves, loads and stores
#
# Register numbers: Sd = Vd:D, Dd = D:Vd (and N, M likewise). With 16 double
# registers, a D register number of 16 or more is UNDEFINED (VFPSmallRegisterBank()).
# ---------------------------------------------------------------------------

S_D, S_N, S_M = "thumb_sreg({Vd}, {D})", "thumb_sreg({Vn}, {N})", "thumb_sreg({Vm}, {M})"
D_D, D_N, D_M = "thumb_dreg({D}, {Vd})", "thumb_dreg({N}, {Vn})", "thumb_dreg({M}, {Vm})"
D16_UNDEF = "VFPSmallRegisterBank() && (%s)"


def fp(id, instr, call, syntax, ops, dp, d=None, n=None, m=None, imm64="0U", sel_cond="0U", **kw):
    regs = []
    undef = []
    undef_c = []
    for var, single, double, bit in (("d", S_D, D_D, "D"), ("n", S_N, D_N, "N"), ("m", S_M, D_M, "M")):
        if {"d": d, "n": n, "m": m}[var]:
            regs.append((var, double if dp else single))
            if dp:
                undef.append("%s == '1'" % bit)
                undef_c.append("({%s} != 0U)" % bit)
    if dp and undef:
        kw.setdefault("undef", D16_UNDEF % " || ".join(undef))
        kw.setdefault("undef_c", " || ".join(undef_c))
    entry(id, instr, syntax, ops, "fp", call, regs,
          {"dp_operation": "TRUE" if dp else "FALSE", "d": "d" if d else "0U", "n": "n" if n else "0U",
           "m": "m" if m else "0U", "imm64": imm64, "sel_cond": sel_cond}, **kw)


for dp, sz in ((False, "S"), (True, "D")):
    f = "F64" if dp else "F32"
    ops = "dp_operation = (sz == '1'); d = %s; m = %s;" % (("UInt(D:Vd)", "UInt(M:Vm)") if dp else ("UInt(Vd:D)", "UInt(Vm:M)"))
    fp("VMOV_r_T2_" + sz, "VMOV", "vmov_reg", "VMOV<c>.%s <%sd>, <%sm>" % (f, sz, sz), ops, dp, d=True, m=True)
    fp("VABS_T2_" + sz, "VABS", "vabs", "VABS<c>.%s <%sd>, <%sm>" % (f, sz, sz), ops, dp, d=True, m=True)
    fp("VNEG_T2_" + sz, "VNEG", "vneg", "VNEG<c>.%s <%sd>, <%sm>" % (f, sz, sz), ops, dp, d=True, m=True)
    fp("VMOV_i_T2_" + sz, "VMOV", "vmov_imm", "VMOV<c>.%s <%sd>, #<imm>" % (f, sz),
       "dp_operation = (sz == '1'); d = %s; imm%d = VFPExpandImm(imm4H:imm4L, %d);"
       % ("UInt(D:Vd)" if dp else "UInt(Vd:D)", 64 if dp else 32, 64 if dp else 32), dp, d=True,
       imm64="VFPExpandImm(({imm4H} << 4) | {imm4L}, %dU)" % (64 if dp else 32))
    for cc, code in (("EQ", "0x0U"), ("VS", "0x6U"), ("GE", "0xAU"), ("GT", "0xCU")):
        fp("VSEL%s_T1_%s" % (cc, sz), "VSEL", "vsel", "VSEL%s.%s <%sd>, <%sn>, <%sm>" % (cc, f, sz, sz, sz),
           "EncodingSpecificOperations(): cond = cc:(cc<1> EOR cc<0>):'0'; dp_operation = (sz == '1');"
           " d, n, m by the size;", dp, d=True, n=True, m=True, sel_cond=code,
           unpred="InITBlock()", unpred_c="Stm32InITBlock(core)",
           note="(VSEL is not conditional: its condition (in.cond) is AL, the selection's is sel_cond.)")

# Core register <-> floating-point register
FP_T_UNPRED, FP_T_UNPRED_C = "t IN {13,15}", "thumb_sp_or_pc(t)"
for id, to_arm in (("VMOV_s_T1", "TRUE"), ("VMOV_tos_T1", "FALSE")):
    entry(id, "VMOV", "VMOV<c> <Rt>, <Sn>" if to_arm == "TRUE" else "VMOV<c> <Sn>, <Rt>",
          "to_arm_registers = (op == '1'); t = UInt(Rt); n = UInt(Vn:N);", "fp_core", "vmov_core_s",
          [("t", "{Rt}"), ("n", S_N)], {"to_arm_registers": to_arm, "Rt": "t", "Rt2": None, "fp": "n", "index": "0U"},
          unpred=FP_T_UNPRED, unpred_c=FP_T_UNPRED_C)
for id, to_arm in (("VMOV_ss_T1", "TRUE"), ("VMOV_toss_T1", "FALSE")):
    entry(id, "VMOV", "VMOV<c> <Rt>, <Rt2>, <Sm>, <Sm1>" if to_arm == "TRUE" else "VMOV<c> <Sm>, <Sm1>, <Rt>, <Rt2>",
          "to_arm_registers = (op == '1'); t = UInt(Rt); t2 = UInt(Rt2); m = UInt(Vm:M);", "fp_core", "vmov_core_ss",
          [("t", "{Rt}"), ("t2", "{Rt2}"), ("m", S_M)],
          {"to_arm_registers": to_arm, "Rt": "t", "Rt2": "t2", "fp": "m", "index": "0U"},
          unpred="t IN {13,15} || t2 IN {13,15} || m == 31" + ("; to_arm_registers && t == t2" if to_arm == "TRUE" else ""),
          unpred_c="thumb_sp_or_pc(t) || thumb_sp_or_pc(t2) || (m == 31U)" + (" || (t == t2)" if to_arm == "TRUE" else ""))
for id, to_arm in (("VMOV_d_T1", "TRUE"), ("VMOV_tod_T1", "FALSE")):
    entry(id, "VMOV", "VMOV<c> <Rt>, <Rt2>, <Dm>" if to_arm == "TRUE" else "VMOV<c> <Dm>, <Rt>, <Rt2>",
          "to_arm_registers = (op == '1'); t = UInt(Rt); t2 = UInt(Rt2); m = UInt(M:Vm);", "fp_core", "vmov_core_d",
          [("t", "{Rt}"), ("t2", "{Rt2}"), ("m", D_M)],
          {"to_arm_registers": to_arm, "Rt": "t", "Rt2": "t2", "fp": "m", "index": "0U"},
          undef=D16_UNDEF % "M == '1'", undef_c="{M} != 0U",
          unpred="t IN {13,15} || t2 IN {13,15}" + ("; to_arm_registers && t == t2" if to_arm == "TRUE" else ""),
          unpred_c="thumb_sp_or_pc(t) || thumb_sp_or_pc(t2)" + (" || (t == t2)" if to_arm == "TRUE" else ""))
entry("VMOV_rs_T1", "VMOV", "VMOV<c>.32 <Dd[x]>, <Rt>",
      "if opc1<1> == '0' && opc2 == '00' then index = UInt(opc1<0>) (32-bit scalar); d = UInt(D:Vd); t = UInt(Rt);",
      "fp_core", "vmov_core_scalar", [("t", "{Rt}"), ("d", D_D)],
      {"to_arm_registers": "FALSE", "Rt": "t", "Rt2": None, "fp": "d", "index": "{opc1} & 1U"},
      undef="opc1<1> == '1' || opc2 != '00' (8- and 16-bit scalars: Advanced SIMD); " + D16_UNDEF % "D == '1'",
      undef_c="(({opc1} & 2U) != 0U) || ({opc2} != 0U) || ({D} != 0U)",
      unpred=FP_T_UNPRED, unpred_c=FP_T_UNPRED_C)
entry("VMOV_sr_T1", "VMOV", "VMOV<c>.32 <Rt>, <Dn[x]>",
      "if U == '0' && opc1<1> == '0' && opc2 == '00' then index = UInt(opc1<0>) (32-bit scalar); t = UInt(Rt); n = UInt(N:Vn);",
      "fp_core", "vmov_core_scalar", [("t", "{Rt}"), ("n", D_N)],
      {"to_arm_registers": "TRUE", "Rt": "t", "Rt2": None, "fp": "n", "index": "{opc1} & 1U"},
      undef="U == '1' || opc1<1> == '1' || opc2 != '00' (8- and 16-bit scalars: Advanced SIMD); " + D16_UNDEF % "N == '1'",
      undef_c="({U} != 0U) || (({opc1} & 2U) != 0U) || ({opc2} != 0U) || ({N} != 0U)",
      unpred=FP_T_UNPRED, unpred_c=FP_T_UNPRED_C)
entry("VMRS_T1_AS", "VMRS", "VMRS<c> <Rt>, FPSCR / VMRS<c> APSR_nzcv, FPSCR", "t = UInt(Rt);", "fp_core", "vmrs_vmsr",
      [("t", "{Rt}")], {"to_arm_registers": "TRUE", "Rt": "t", "Rt2": None, "fp": "0U", "index": "0U"},
      unpred="t == 13; reg != '0001' (FPSCR is the only floating-point system register of Armv7-M)",
      unpred_c="(t == 13U) || ({reg} != 1U)")
entry("VMSR_T1_AS", "VMSR", "VMSR<c> FPSCR, <Rt>", "t = UInt(Rt);", "fp_core", "vmrs_vmsr",
      [("t", "{Rt}")], {"to_arm_registers": "FALSE", "Rt": "t", "Rt2": None, "fp": "0U", "index": "0U"},
      unpred="t IN {13,15}; reg != '0001' (FPSCR is the only floating-point system register of Armv7-M)",
      unpred_c="thumb_sp_or_pc(t) || ({reg} != 1U)")


# Loads and stores
def fp_ls(id, instr, call, syntax, ops, single, d, n, imm32, add, wback="FALSE", regs="0U", regs_var=None, **kw):
    more = [("regs", regs_var)] if regs_var else []
    entry(id, instr, syntax, ops, "fp_ls", call, [("d", d), ("n", n)] + more,
          {"single_regs": single, "d": "d", "Rn": "n", "imm32": imm32, "add": add, "wback": wback,
           "regs": "regs" if regs_var else regs}, **kw)


for sz, single, dreg in (("S", "TRUE", S_D), ("D", "FALSE", D_D)):
    d16 = {} if single == "TRUE" else {"undef": D16_UNDEF % "D == '1'", "undef_c": "{D} != 0U"}
    reg = "UInt(Vd:D)" if single == "TRUE" else "UInt(D:Vd)"
    fp_ls("VLDR_T1_" + sz, "VLDR", "vldr", "VLDR<c> <%sd>, [<Rn>{, #+/-<imm>}]" % sz,
          "single_reg = %s; add = (U == '1'); imm32 = ZeroExtend(imm8:'00', 32); d = %s; n = UInt(Rn);" % (single, reg),
          single, dreg, "{Rn}", "{imm8} << 2", "{U} != 0U", **d16)
    fp_ls("VLDR_l_T1_" + sz, "VLDR", "vldr", "VLDR<c> <%sd>, <label>" % sz,
          "single_reg = %s; add = (U == '1'); imm32 = ZeroExtend(imm8:'00', 32); d = %s; n = 15;" % (single, reg),
          single, dreg, "15U", "{imm8} << 2", "{U} != 0U", **d16)
    fp_ls("VSTR_T1_" + sz, "VSTR", "vstr", "VSTR<c> <%sd>, [<Rn>{, #+/-<imm>}]" % sz,
          "single_reg = %s; add = (U == '1'); imm32 = ZeroExtend(imm8:'00', 32); d = %s; n = UInt(Rn);" % (single, reg),
          single, dreg, "{Rn}", "{imm8} << 2", "{U} != 0U", unpred="n == 15", unpred_c="n == 15U", **d16)
for t, single, dreg in (("T1", "FALSE", D_D), ("T2", "TRUE", S_D)):
    reg = "UInt(Vd:D)" if single == "TRUE" else "UInt(D:Vd)"
    regs_ops = "regs = UInt(imm8)" if single == "TRUE" else "regs = UInt(imm8) DIV 2"
    regs_var = "{imm8}" if single == "TRUE" else "{imm8} >> 1"
    if single == "TRUE":
        unpred = "n == 15; regs == 0 || (d+regs) > 32"
        unpred_c = "(n == 15U) || (regs == 0U) || ((d + regs) > 32U)"
    else:
        unpred = ("n == 15; regs == 0 || regs > 16 || (d+regs) > 32; imm8<0> == '1' && (d+regs) > 16;"
                  " VFPSmallRegisterBank() && (d+regs) > 16")
        unpred_c = "(n == 15U) || (regs == 0U) || ((d + regs) > 16U)"
    for instr, call, form, add, wback in (("VLDM", "vldm", "", "TRUE", "{W} != 0U"), ("VLDMDB", "vldm", "DB", "FALSE", "TRUE"),
                                          ("VSTM", "vstm", "", "TRUE", "{W} != 0U"), ("VSTMDB", "vstm", "DB", "FALSE", "TRUE")):
        fp_ls("%s_%s" % (instr, t), instr, call, "%s<c> <Rn>%s, <list>" % (instr, "{!}" if not form else "!"),
              "single_regs = %s; add = %s; wback = %s; d = %s; n = UInt(Rn); imm32 = ZeroExtend(imm8:'00', 32); %s;"
              % (single, "(U == '1')" if not form else "FALSE", "(W == '1')" if not form else "TRUE", reg, regs_ops),
              single, dreg, "{Rn}", "{imm8} << 2", add, wback, regs_var=regs_var, unpred=unpred, unpred_c=unpred_c,
              note="(Odd imm8 with D registers is FLDMX/FSTMX: its (d+regs) > 16 case is within the D16 one.)"
              if single == "FALSE" else None)

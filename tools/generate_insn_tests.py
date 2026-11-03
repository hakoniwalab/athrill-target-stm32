#!/usr/bin/env python3
"""Generate instruction test programs (test/insn/<group>.S) from case
templates, with random operands from a fixed seed.

Every case sets r0-r6, r8-r12 and LR to random values, runs one instruction
(the encoding under test, checked afterwards by tools/check_insn_tests.py),
then prints all of them (and, for stores, the memory it wrote) on one line.
The expected output comes from QEMU (test/insn/build.sh), so a case needs
no expected value here.

Memory used by the cases (test/insn/link.ld: stack from 0x20001000 down):
  pattern  256 random bytes in flash, read by the loads
  0x20000400..0x200007FF  store slots (16 bytes per case), cleared at start
  0x20000800..0x2000083F  register dump

Usage:
  python tools/generate_insn_tests.py
"""
import os
import random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "test", "insn")
SEED = 20261103
CASES = 3

DUMP_REGS = ["r0", "r1", "r2", "r3", "r4", "r5", "r6", "r8", "r9", "r10", "r11", "r12", "lr"]
SET_REGS = ["r0", "r1", "r2", "r3", "r4", "r5", "r6", "r8", "r9", "r10", "r11", "r12", "lr"]


class Case:
    def __init__(self, name, insn, setup=(), slot=False, pre=(), post=(), flags=False):
        self.name = name
        self.flags = flags        # print NZCV after the instruction
        self.insn = insn          # the instruction under test
        self.setup = list(setup)  # (register, value) after the random values
        self.slot = slot          # print the 16-byte store slot
        self.pre = list(pre)      # lines before the instruction (after setup)
        self.post = list(post)    # lines after it (before the dump)


def rnd(rng):
    return rng.getrandbits(32)


# ---------------------------------------------------------------------------
# Load and store cases
# ---------------------------------------------------------------------------

def ls_cases(rng):
    cases = []
    slot = [0]

    def next_slot():
        address = 0x20000400 + 16 * slot[0]
        slot[0] += 1
        return address

    def pat(offset):
        return "pattern + %d" % offset

    # 16-bit immediate: <Rt>, [<Rn>, #imm] with imm = imm5 * scale
    for mnem, scale in (("ldr", 4), ("ldrb", 1), ("ldrh", 2)):
        for i in range(CASES):
            imm = rng.randrange(32) * scale
            base = rng.randrange(0, 256 - imm - 4)
            cases.append(Case("%s_i_t1_%d" % (mnem, i), "%s r0, [r1, #%d]" % (mnem, imm), [("r1", pat(base))]))
    for mnem, scale in (("str", 4), ("strb", 1), ("strh", 2)):
        for i in range(CASES):
            imm = rng.randrange(4) * scale
            address = next_slot()
            cases.append(Case("%s_i_t1_%d" % (mnem, i), "%s r0, [r1, #%d]" % (mnem, imm),
                              [("r1", "0x%08X" % (address + rng.randrange(4)))], slot=address))
    # SP-relative (T2): the stack is cleared RAM
    for i in range(CASES):
        imm = rng.randrange(16) * 4
        cases.append(Case("str_i_t2_%d" % i, "str r0, [sp, #%d]" % imm))
        cases.append(Case("ldr_i_t2_%d" % i, "ldr r2, [sp, #%d]" % imm))
    # 16-bit register
    for mnem in ("ldr", "ldrb", "ldrh", "ldrsb", "ldrsh"):
        for i in range(CASES):
            off = rng.randrange(0, 200)
            base = rng.randrange(0, 50)
            cases.append(Case("%s_r_t1_%d" % (mnem, i), "%s r0, [r1, r2]" % mnem,
                              [("r1", pat(base)), ("r2", "%d" % off)]))
    for mnem in ("str", "strb", "strh"):
        for i in range(CASES):
            address = next_slot()
            cases.append(Case("%s_r_t1_%d" % (mnem, i), "%s r0, [r1, r2]" % mnem,
                              [("r1", "0x%08X" % (address - 100)), ("r2", "%d" % (100 + rng.randrange(8)))], slot=address))
    # LDR (literal) T1: a data word right after the instruction
    for i in range(CASES):
        cases.append(Case("ldr_l_t1_%d" % i, "ldr r0, 1f", post=["b 2f", ".align 2", "1: .word 0x%08X" % rnd(rng), "2:"]))
    # 32-bit, 12-bit immediate
    for mnem in ("ldr", "ldrb", "ldrh", "ldrsb", "ldrsh"):
        for i in range(CASES):
            imm = rng.randrange(0, 4096)
            base = rng.randrange(0, 250)
            cases.append(Case("%s_i_w12_%d" % (mnem, i), "%s.w r0, [r1, #%d]" % (mnem, imm),
                              [("r1", "pattern + %d - %d" % (base, imm))]))
    for mnem in ("str", "strb", "strh"):
        for i in range(CASES):
            imm = rng.randrange(0, 4096)
            address = next_slot()
            cases.append(Case("%s_i_w12_%d" % (mnem, i), "%s.w r0, [r1, #%d]" % (mnem, imm),
                              [("r1", "0x%08X" % (address - imm + rng.randrange(4)))], slot=address))
    # 32-bit, 8-bit immediate: [Rn, #-imm8], [Rn], #+/-imm8, [Rn, #+/-imm8]!
    for mnem in ("ldr", "ldrb", "ldrh", "ldrsb", "ldrsh"):
        for i in range(CASES):
            imm = rng.randrange(1, 256)
            base = rng.randrange(0, 250)
            sign = rng.choice(("", "-"))
            cases.append(Case("%s_i_off_%d" % (mnem, i), "%s r0, [r1, #-%d]" % (mnem, imm),
                              [("r1", "pattern + %d + %d" % (base, imm))]))
            post_base = base + imm if sign == "-" else base
            cases.append(Case("%s_i_post_%d" % (mnem, i), "%s r0, [r1], #%s%d" % (mnem, sign, imm),
                              [("r1", pat(base))]))
            pre_base = base + imm if sign == "-" else base - imm if base >= imm else base
            pre_imm = imm if (sign == "-" or base >= imm) else 0
            cases.append(Case("%s_i_pre_%d" % (mnem, i), "%s r0, [r1, #%s%d]!" % (mnem, sign, pre_imm),
                              [("r1", "pattern + %d" % pre_base)]))
            del post_base
    for mnem in ("str", "strb", "strh"):
        for i in range(CASES):
            imm = rng.randrange(1, 16)
            address = next_slot()
            cases.append(Case("%s_i_off_%d" % (mnem, i), "%s r0, [r1, #-%d]" % (mnem, imm),
                              [("r1", "0x%08X" % (address + imm))], slot=address))
            address = next_slot()
            sign = rng.choice(("", "-"))
            cases.append(Case("%s_i_post_%d" % (mnem, i), "%s r0, [r1], #%s%d" % (mnem, sign, imm),
                              [("r1", "0x%08X" % (address + rng.randrange(4)))], slot=address))
            address = next_slot()
            cases.append(Case("%s_i_pre_%d" % (mnem, i), "%s r0, [r1, #%s%d]!" % (mnem, sign, imm),
                              [("r1", "0x%08X" % (address + (imm if sign == "-" else -imm) + 4))], slot=address))
    # Unprivileged (offset form)
    for mnem in ("ldrt", "ldrbt", "ldrht", "ldrsbt", "ldrsht"):
        for i in range(CASES):
            imm = rng.randrange(256)
            cases.append(Case("%s_%d" % (mnem, i), "%s r0, [r1, #%d]" % (mnem, imm),
                              [("r1", pat(rng.randrange(0, 250 - imm) if imm < 250 else 0) + " - 0")]))
    for mnem in ("strt", "strbt", "strht"):
        for i in range(CASES):
            imm = rng.randrange(8)
            address = next_slot()
            cases.append(Case("%s_%d" % (mnem, i), "%s r0, [r1, #%d]" % (mnem, imm),
                              [("r1", "0x%08X" % (address))], slot=address))
    # Literal, 32-bit: data after (U = 1) and before (U = 0) the instruction
    for mnem in ("ldr", "ldrb", "ldrh", "ldrsb", "ldrsh"):
        for i in range(CASES):
            value = rnd(rng)
            cases.append(Case("%s_l_fwd_%d" % (mnem, i), "%s.w r0, 1f" % mnem,
                              post=["b 2f", ".align 2", "1: .word 0x%08X" % value, "2:"]))
            cases.append(Case("%s_l_back_%d" % (mnem, i), "%s.w r0, 1b" % mnem,
                              pre=["b 2f", ".align 2", "1: .word 0x%08X" % rnd(rng), "2:"]))
    # 32-bit register, LSL #imm2
    for mnem in ("ldr", "ldrb", "ldrh", "ldrsb", "ldrsh"):
        for i in range(CASES):
            sh = rng.randrange(4)
            off = rng.randrange(0, 60)
            cases.append(Case("%s_r_w_%d" % (mnem, i), "%s.w r0, [r1, r2, lsl #%d]" % (mnem, sh),
                              [("r1", pat(rng.randrange(0, 60))), ("r2", "%d" % off)]))
    for mnem in ("str", "strb", "strh"):
        for i in range(CASES):
            sh = rng.randrange(4)
            address = next_slot()
            cases.append(Case("%s_r_w_%d" % (mnem, i), "%s.w r0, [r1, r2, lsl #%d]" % (mnem, sh),
                              [("r1", "0x%08X" % (address - (2 << sh))), ("r2", "2")], slot=address))
    # Dual
    for i in range(CASES):
        imm = rng.randrange(0, 32) * 4
        cases.append(Case("ldrd_off_%d" % i, "ldrd r0, r2, [r1, #%d]" % imm, [("r1", "pattern_aligned + %d" % (4 * rng.randrange(16)))]))
        cases.append(Case("ldrd_pre_%d" % i, "ldrd r0, r2, [r1, #-%d]!" % imm, [("r1", "pattern_aligned + %d + %d" % (imm, 4 * rng.randrange(16)))]))
        cases.append(Case("ldrd_post_%d" % i, "ldrd r0, r2, [r1], #%d" % imm, [("r1", "pattern_aligned + %d" % (4 * rng.randrange(16)))]))
        k = imm % 16
        for form, operand, base in (("off", "[r1, #%d]" % k, -k), ("pre", "[r1, #-%d]!" % k, k), ("post", "[r1], #%d" % k, 0)):
            address = next_slot()
            cases.append(Case("strd_%s_%d" % (form, i), "strd r0, r2, %s" % operand,
                              [("r1", "0x%08X" % (address + base))], slot=address))
        cases.append(Case("ldrd_l_%d" % i, "ldrd r0, r2, 1f",
                          post=["b 2f", ".align 3", "1: .word 0x%08X, 0x%08X" % (rnd(rng), rnd(rng)), "2:"]))
    # Multiple
    for i in range(CASES):
        lst = sorted(rng.sample(["r0", "r2", "r3", "r4", "r5", "r6"], 3))
        cases.append(Case("ldm_t1_%d" % i, "ldm r1!, {%s}" % ", ".join(lst), [("r1", "pattern_aligned + %d" % (4 * rng.randrange(16)))]))
        address = next_slot()
        cases.append(Case("stm_t1_%d" % i, "stm r1!, {%s}" % ", ".join(lst), [("r1", "0x%08X" % address)], slot=address))
        cases.append(Case("push_pop_t1_%d" % i, "push {%s, lr}" % ", ".join(lst),
                          post=["pop {r0, r2, r3, r4}"]))
        wlst = sorted(rng.sample(["r0", "r2", "r3", "r4", "r5", "r6", "r8", "r9", "r10", "r11", "r12", "lr"], 4),
                      key=lambda r: ["r0", "r2", "r3", "r4", "r5", "r6", "r8", "r9", "r10", "r11", "r12", "lr"].index(r))
        cases.append(Case("ldm_w_%d" % i, "ldm.w r1%s, {%s}" % ("!" if i % 2 else "", ", ".join(wlst)),
                          [("r1", "pattern_aligned + %d" % (4 * rng.randrange(16)))]))
        cases.append(Case("ldmdb_%d" % i, "ldmdb r1%s, {%s}" % ("!" if i % 2 == 0 else "", ", ".join(wlst)),
                          [("r1", "pattern_aligned + %d" % (64 + 4 * rng.randrange(16)))]))
        address = next_slot()
        cases.append(Case("stm_w_%d" % i, "stm.w r1%s, {%s}" % ("!" if i % 2 else "", ", ".join(wlst[:3])),
                          [("r1", "0x%08X" % address)], slot=address))
        address = next_slot()
        cases.append(Case("stmdb_%d" % i, "stmdb r1%s, {%s}" % ("!" if i % 2 == 0 else "", ", ".join(wlst[:3])),
                          [("r1", "0x%08X" % (address + 12))], slot=address))
        cases.append(Case("push_pop_w_%d" % i, "push.w {%s}" % ", ".join(wlst[:3]),
                          post=["pop.w {r8, r9, r10}"]))
    # Exclusive
    for i in range(CASES):
        address = next_slot()
        a = "0x%08X" % address
        cases.append(Case("ldrex_strex_%d" % i, "strex r2, r3, [r1, #4]", [("r1", a)], slot=address,
                          pre=["ldrex r0, [r1, #4]"], post=["strex r4, r6, [r1, #4]"]))
        address = next_slot()
        a = "0x%08X" % address
        cases.append(Case("ldrexb_strexb_%d" % i, "strexb r2, r3, [r1]", [("r1", a)], slot=address,
                          pre=["ldrexb r0, [r1]"]))
        address = next_slot()
        a = "0x%08X" % address
        cases.append(Case("ldrexh_strexh_%d" % i, "strexh r2, r3, [r1]", [("r1", "0x%08X" % (address + 2))], slot=address,
                          pre=["ldrexh r0, [r1]"]))
        address = next_slot()
        a = "0x%08X" % address
        cases.append(Case("clrex_%d" % i, "clrex", [("r1", a)], slot=address, pre=["ldrex r0, [r1]"],
                          post=["strex r2, r3, [r1]"]))
    # Table branches: the table is right after the instruction
    for i in range(CASES):
        idx = rng.randrange(3)
        cases.append(Case("tbb_%d" % i, "tbb [pc, r1]", [("r1", "%d" % idx)],
                          post=["1: .byte (2f-1b)/2, (3f-1b)/2, (4f-1b)/2", ".align 1",
                                "2: movs r0, #1", "b 5f", "3: movs r0, #2", "b 5f", "4: movs r0, #3", "5:"]))
        cases.append(Case("tbh_%d" % i, "tbh [pc, r1, lsl #1]", [("r1", "%d" % idx)],
                          post=["1: .hword (2f-1b)/2, (3f-1b)/2, (4f-1b)/2",
                                "2: movs r0, #1", "b 5f", "3: movs r0, #2", "b 5f", "4: movs r0, #3", "5:"]))
    # ADR (16-bit): a word-aligned address after the instruction
    for i in range(CASES):
        cases.append(Case("adr_t1_%d" % i, "adr r0, 1f", post=["b 2f", ".align 2", "1: .word 0", "2:"]))
    # Memory hints
    for k, insn in enumerate(("pld [r1, #100]", "pld [r1, #-10]", "pld 1f", "pld [r1, r2, lsl #1]",
                              "pli [r1, #100]", "pli [r1, #-10]", "pli 1f", "pli [r1, r2, lsl #1]")):
        cases.append(Case("hint_%s_%d" % (insn.split()[0], k), insn, [("r1", pat(20)), ("r2", "3")],
                          post=["b 2f", ".align 2", "1: .word 0", "2:"] if "1f" in insn else []))
    return cases


# ---------------------------------------------------------------------------
# Branch cases: a taken branch skips "movs r0, #1" (or r6), so r0 keeps its
# random value; LR shows the return address of BL and BLX.
# ---------------------------------------------------------------------------

def br_cases(rng):
    cases = []
    for i in range(CASES):
        # BL forward and backward, near and over a gap (larger imm10)
        gap = 2 * rng.randrange(0x400, 0x4000)
        cases.append(Case("bl_fwd_%d" % i, "bl 1f", post=["movs r0, #1", "1:"]))
        cases.append(Case("bl_fwd_far_%d" % i, "bl 1f", post=["movs r0, #1", ".space 0x%X" % gap, "1:"]))
        cases.append(Case("bl_back_%d" % i, "bl 1b", pre=["b 2f", "1: movs r1, #7", "b 3f", "2:"],
                          post=["movs r1, #9", "3:"]))
        cases.append(Case("bl_back_far_%d" % i, "bl 1b",
                          pre=["b.w 2f", "1: movs r1, #7", "b.w 3f", ".space 0x%X" % gap, "2:"],
                          post=["movs r1, #9", "3:"]))
        # BLX <Rm>: the target is a Thumb address (bit 0 set); Rm = LR is read before LR is written
        for reg in ("r3", "r9", "lr"):
            pre = ["adr r5, 1f", "adds r5, #1"] + ([] if reg == "r5" else ["mov %s, r5" % reg])
            cases.append(Case("blx_%s_%d" % (reg, i), "blx %s" % reg, pre=pre,
                              post=["movs r0, #1", ".align 2", "1:"]))
        # CBZ / CBNZ, taken and not taken, forward by up to 126 bytes
        for mnem in ("cbz", "cbnz"):
            for zero in (True, False):
                reg = rng.choice(["r1", "r2", "r3", "r4", "r5"])
                gap = 2 * rng.randrange(0, 61)
                cases.append(Case("%s_%s_%d" % (mnem, "zero" if zero else "nonzero", i), "%s %s, 1f" % (mnem, reg),
                                  [(reg, "0" if zero else "0x%08X" % (rnd(rng) | 1))],
                                  post=["movs r6, #1", "b 1f", ".space %d" % gap, "1:"]))
    return cases


# ---------------------------------------------------------------------------
# SP-relative ADD/SUB, ADR, MOVT, extend, bitfield, bit operations, multiply
# and divide. An instruction that writes SP is followed by "mov r0, sp" and
# SP is put back from r6.
# ---------------------------------------------------------------------------

EDGE = [0, 1, 0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF]


def value(rng):
    return rng.choice(EDGE) if rng.random() < 0.3 else rnd(rng)


def thumb_const(rng):
    """A random 32-bit constant that ThumbExpandImm can encode."""
    b = rng.randrange(1, 256)
    kind = rng.randrange(5)
    if kind == 0:
        return b
    if kind == 1:
        return b | (b << 16)
    if kind == 2:
        return (b << 8) | (b << 24)
    if kind == 3:
        return b * 0x01010101
    return ((0x80 | rng.randrange(128)) << rng.randrange(0, 25)) & 0xFFFFFFFF


def mx_cases(rng):
    cases = []
    sp_save = ["mov r6, sp"]
    sp_restore = ["mov r0, sp", "mov sp, r6"]
    for i in range(CASES):
        # SP plus register / immediate, 16-bit
        cases.append(Case("add_sp_r_t1_%d" % i, "add r2, sp, r2", [("r2", "%d" % (4 * rng.randrange(64)))]))
        cases.append(Case("add_sp_r_t2_%d" % i, "add sp, r2", [("r2", "%d" % (4 * rng.randrange(-64, 64)))],
                          pre=sp_save, post=sp_restore))
        cases.append(Case("add_sp_i_t1_%d" % i, "add r0, sp, #%d" % (4 * rng.randrange(256))))
        cases.append(Case("add_sp_i_t2_%d" % i, "add sp, #%d" % (4 * rng.randrange(128)), pre=sp_save, post=sp_restore))
        cases.append(Case("sub_sp_i_t1_%d" % i, "sub sp, #%d" % (4 * rng.randrange(128)), pre=sp_save, post=sp_restore))
        # SP plus register / immediate, 32-bit
        for mnem in ("add", "sub"):
            for s_ in ("", "s"):
                sh = rng.choice(("lsl", "lsr", "asr", "ror"))
                amount = rng.randrange(1, 32)
                cases.append(Case("%s%s_sp_r_w_%d" % (mnem, s_, i), "%s%s.w r0, sp, r2, %s #%d" % (mnem, s_, sh, amount),
                                  [("r2", "0x%08X" % value(rng))], flags=bool(s_)))
                cases.append(Case("%s%s_sp_r_rrx_%d" % (mnem, s_, i), "%s%s.w r0, sp, r2, rrx" % (mnem, s_),
                                  [("r2", "0x%08X" % value(rng))], pre=["cmp r3, r4"], flags=bool(s_)))
                cases.append(Case("%s%s_sp_i_w_%d" % (mnem, s_, i), "%s%s.w r0, sp, #0x%X" % (mnem, s_, thumb_const(rng)),
                                  flags=bool(s_)))
            cases.append(Case("%s_sp_sp_r_w_%d" % (mnem, i), "%s.w sp, sp, r2, lsl #%d" % (mnem, rng.randrange(4)),
                              [("r2", "%d" % (4 * rng.randrange(16)))], pre=sp_save, post=sp_restore))
            cases.append(Case("%sw_sp_%d" % (mnem, i), "%sw r0, sp, #%d" % (mnem, rng.randrange(4096))))
        # ADR (32-bit): back (T2, subtract) and forward (T3, add) over a gap
        gap = 4 * rng.randrange(1, 200)
        cases.append(Case("adr_w_back_%d" % i, "adr.w r0, 1b", pre=["b 2f", ".align 2", "1: .space %d" % gap, "2:"]))
        cases.append(Case("adr_w_fwd_%d" % i, "adr.w r0, 1f", post=["b 2f", ".space %d" % gap, ".align 2", "1:", "2:"]))
        cases.append(Case("movt_%d" % i, "movt r0, #0x%X" % rng.randrange(0x10000)))
        # Extend
        for mnem in ("sxtb", "sxth", "uxtb", "uxth", "rev", "rev16", "revsh"):
            cases.append(Case("%s_t1_%d" % (mnem, i), "%s r0, r1" % mnem, [("r1", "0x%08X" % value(rng))]))
        for mnem in ("sxtb", "sxth", "uxtb", "uxth"):
            cases.append(Case("%s_w_%d" % (mnem, i), "%s.w r0, r1, ror #%d" % (mnem, 8 * rng.randrange(4)),
                              [("r1", "0x%08X" % value(rng))]))
        for mnem in ("sxtb16", "uxtb16"):
            cases.append(Case("%s_%d" % (mnem, i), "%s r0, r1, ror #%d" % (mnem, 8 * rng.randrange(4)),
                              [("r1", "0x%08X" % value(rng))]))
        for mnem in ("sxtab", "sxtah", "uxtab", "uxtah", "sxtab16", "uxtab16"):
            cases.append(Case("%s_%d" % (mnem, i), "%s r0, r1, r2, ror #%d" % (mnem, 8 * rng.randrange(4)),
                              [("r1", "0x%08X" % value(rng)), ("r2", "0x%08X" % value(rng))]))
        # Bitfield
        for mnem in ("sbfx", "ubfx", "bfi"):
            lsb = rng.randrange(32)
            width = rng.randrange(1, 33 - lsb)
            cases.append(Case("%s_%d" % (mnem, i), "%s r0, r1, #%d, #%d" % (mnem, lsb, width),
                              [("r1", "0x%08X" % value(rng))]))
        lsb = rng.randrange(32)
        cases.append(Case("bfc_%d" % i, "bfc r0, #%d, #%d" % (lsb, rng.randrange(1, 33 - lsb))))
        # Bit operations (32-bit)
        for mnem in ("rev.w", "rev16.w", "revsh.w", "rbit", "clz"):
            cases.append(Case("%s_%d" % (mnem.replace(".w", "_w"), i), "%s r0, r1" % mnem,
                              [("r1", "0x%08X" % (value(rng) >> rng.randrange(32)))]))
        # Multiply and divide
        cases.append(Case("muls_t1_%d" % i, "muls r0, r1, r0", [("r0", "0x%08X" % value(rng)), ("r1", "0x%08X" % value(rng))],
                          flags=True))
        for mnem, ops in (("mul", "r0, r1, r2"), ("mla", "r0, r1, r2, r3"), ("mls", "r0, r1, r2, r3"),
                          ("smull", "r0, r4, r1, r2"), ("umull", "r0, r4, r1, r2"),
                          ("smlal", "r0, r4, r1, r2"), ("umlal", "r0, r4, r1, r2"),
                          ("sdiv", "r0, r1, r2"), ("udiv", "r0, r1, r2")):
            cases.append(Case("%s_%d" % (mnem, i), "%s %s" % (mnem, ops),
                              [("r1", "0x%08X" % value(rng)), ("r2", "0x%08X" % value(rng))]))
    for mnem in ("sdiv", "udiv"):
        cases.append(Case("%s_by_zero" % mnem, "%s r0, r1, r2" % mnem, [("r2", "0")]))
    cases.append(Case("sdiv_overflow", "sdiv r0, r1, r2", [("r1", "0x80000000"), ("r2", "0xFFFFFFFF")]))
    cases.append(Case("sdiv_negative", "sdiv r0, r1, r2", [("r1", "0xFFFFFF9C"), ("r2", "7")]))
    cases.append(Case("smlal_same_sign", "smlal r0, r4, r1, r2", [("r1", "0x80000000"), ("r2", "0x80000000")]))
    return cases


# ---------------------------------------------------------------------------
# IT blocks, MRS and MSR (program status registers), barriers and hints.
# An IT block's flags are set with MSR first; its instructions are chosen at
# random among ones whose condition comes from the IT state.
# ---------------------------------------------------------------------------

CONDS = ["eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le"]
INVERSE = {c: CONDS[i ^ 1] for i, c in enumerate(CONDS)}


def sy_cases(rng):
    cases = []
    slot = [0]

    def it_case(name, n, last=None, fault_skip=False):
        # fault_skip: every condition fails (EQ, T only, Z clear), so the loads
        # from an address with no memory are not made
        cond = "eq" if fault_skip else rng.choice(CONDS)
        pattern = "".join("t" if fault_skip else rng.choice("te") for _ in range(n - 1))
        conds = [cond] + [cond if p == "t" else INVERSE[cond] for p in pattern]
        address = 0x20000400 + 16 * (64 + slot[0])
        slot[0] += 1
        body = []
        for k, c in enumerate(conds):
            if k == n - 1 and last == "b":
                body.append("b%s 1f" % c)
            elif k == n - 1 and last == "bx":
                body.append("bx%s lr" % c)
            elif fault_skip:
                body.append("ldr%s r0, [r3]" % c)
            else:
                body.append(rng.choice([
                    "add%s r0, r0, r1" % c, "mov%s r2, #%d" % (c, rng.randrange(256)), "ldr%s r3, [r4, #%d]" % (c, 4 * rng.randrange(8)),
                    "str%s r5, [r6, #%d]" % (c, 4 * rng.randrange(4)), "cmp%s r1, r2" % c, "lsl%s r0, r1, #%d" % (c, rng.randrange(1, 32)),
                    "sub%s.w r8, r9, r10" % c, "mul%s r0, r1, r0" % c, "rev%s r1, r2" % c, "adds%s.w r9, r9, r10" % c]))
        nzcv = (rng.getrandbits(4) << 28) & (~0x40000000 if fault_skip else 0xFFFFFFFF)
        pre = []
        if last == "bx":
            pre += ["adr.w r7, 1f", "adds r7, #1", "mov lr, r7"]
        pre += ["ldr.w r7, =0x%08X" % nzcv, "msr APSR_nzcvq, r7", "it%s %s" % (pattern, cond)] + body[:-1]
        post = ["movs r0, #1", "movs r1, #2", ".align 2", "1:"] if last else []
        setup = [("r4", "pattern_aligned + %d" % (4 * rng.randrange(16))), ("r6", "0x%08X" % address)]
        if fault_skip:
            setup.append(("r3", "0xA0000000"))
        cases.append(Case(name, body[-1], setup, slot=address, pre=pre, post=post, flags=True))

    for i in range(24):
        it_case("it_%d" % i, rng.randrange(1, 5))
    for i in range(6):
        it_case("it_b_%d" % i, rng.randrange(1, 5), last="b")
        it_case("it_bx_%d" % i, rng.randrange(1, 5), last="bx")
    # ITTTT AL, written as a halfword (the assembler takes no AL block): the 16-bit
    # forms that set the flags outside an IT block do not inside one.
    for i in range(2):
        cases.append(Case("it_al_%d" % i, "muls r0, r1, r0",
                          [("r0", "0x%08X" % value(rng)), ("r1", "0x%08X" % value(rng))],
                          pre=["ldr.w r7, =0x%08X" % (rng.getrandbits(4) << 28), "msr APSR_nzcvq, r7",
                               ".hword 0xBFE1", "adds r2, r2, r3", "movs r3, #0", "lsls r4, r5, #%d" % rng.randrange(1, 32)],
                          flags=True))
    for i in range(3):
        it_case("it_skip_fault_%d" % i, 2, fault_skip=True)
    # BX outside an IT block
    cases.append(Case("bx_lr", "bx lr", pre=["adr.w r7, 1f", "adds r7, #1", "mov lr, r7"],
                      post=["movs r0, #1", ".align 2", "1:"]))
    # MRS: every program status register, after MSR set N Z C V Q and GE
    for reg in ("apsr", "iapsr", "eapsr", "xpsr", "ipsr", "epsr", "iepsr"):
        for i in range(2):
            cases.append(Case("mrs_%s_%d" % (reg, i), "mrs r0, %s" % reg,
                              pre=["ldr.w r7, =0x%08X" % rnd(rng), "msr APSR_nzcvqg, r7"]))
    # MSR: the APSR fields it writes, read back with MRS
    for reg in ("APSR_nzcvq", "APSR_g", "APSR_nzcvqg", "IAPSR_nzcvq", "EAPSR_nzcvqg", "XPSR_nzcvq", "IPSR", "EPSR", "IEPSR"):
        for i in range(2):
            cases.append(Case("msr_%s_%d" % (reg.lower(), i), "msr %s, r1" % reg, [("r1", "0x%08X" % rnd(rng))],
                              pre=["ldr.w r7, =0x%08X" % rnd(rng), "msr APSR_nzcvqg, r7"], post=["mrs r2, apsr"],
                              flags=True))
    # Barriers and hints
    for insn in ("dmb ish", "dmb sy", "dsb sy", "dsb ishst", "isb sy", "nop.w", "yield", "yield.w", "dbg #5"):
        cases.append(Case("hint_" + insn.replace(" ", "_").replace(".", "_").replace("#", ""), insn))
    return cases


# ---------------------------------------------------------------------------
# DSP: each case starts from random N Z C V Q and GE (MSR) and reads the
# APSR back into r6 (MRS), so GE and Q are compared too.
# ---------------------------------------------------------------------------

def dsp_cases(rng):
    cases = []

    def case(name, insn, regs):
        setup = [(r, "0x%08X" % value(rng)) for r in regs]
        apsr = (rng.getrandbits(5) << 27) | (rng.getrandbits(4) << 16)
        cases.append(Case(name, insn, setup, pre=["ldr.w r7, =0x%08X" % apsr, "msr APSR_nzcvqg, r7"],
                          post=["mrs r6, apsr"]))

    for i in range(CASES):
        for op in ("add16", "asx", "sax", "sub16", "add8", "sub8"):
            for prefix in ("s", "u", "q", "uq", "sh", "uh"):
                case("%s%s_%d" % (prefix, op, i), "%s%s r0, r1, r2" % (prefix, op), ("r1", "r2"))
        for mnem in ("qadd", "qsub", "qdadd", "qdsub", "sel", "usad8"):
            case("%s_%d" % (mnem, i), "%s r0, r1, r2" % mnem, ("r1", "r2"))
        for x in "bt":
            for y in "bt":
                case("smul%s%s_%d" % (x, y, i), "smul%s%s r0, r1, r2" % (x, y), ("r1", "r2"))
                case("smla%s%s_%d" % (x, y, i), "smla%s%s r0, r1, r2, r3" % (x, y), ("r1", "r2", "r3"))
                case("smlal%s%s_%d" % (x, y, i), "smlal%s%s r0, r4, r1, r2" % (x, y), ("r0", "r1", "r2", "r4"))
            case("smulw%s_%d" % (x, i), "smulw%s r0, r1, r2" % x, ("r1", "r2"))
            case("smlaw%s_%d" % (x, i), "smlaw%s r0, r1, r2, r3" % x, ("r1", "r2", "r3"))
        for mnem in ("smuad", "smuadx", "smusd", "smusdx", "smmul", "smmulr"):
            case("%s_%d" % (mnem, i), "%s r0, r1, r2" % mnem, ("r1", "r2"))
        for mnem in ("smlad", "smladx", "smlsd", "smlsdx", "smmla", "smmlar", "smmls", "smmlsr", "usada8"):
            case("%s_%d" % (mnem, i), "%s r0, r1, r2, r3" % mnem, ("r1", "r2", "r3"))
        for mnem in ("smlald", "smlaldx", "smlsld", "smlsldx", "umaal"):
            case("%s_%d" % (mnem, i), "%s r0, r4, r1, r2" % mnem, ("r0", "r1", "r2", "r4"))
        case("ssat_lsl_%d" % i, "ssat r0, #%d, r1, lsl #%d" % (rng.randrange(1, 33), rng.randrange(32)), ("r1",))
        case("ssat_asr_%d" % i, "ssat r0, #%d, r1, asr #%d" % (rng.randrange(1, 33), rng.randrange(1, 32)), ("r1",))
        case("usat_lsl_%d" % i, "usat r0, #%d, r1, lsl #%d" % (rng.randrange(32), rng.randrange(32)), ("r1",))
        case("usat_asr_%d" % i, "usat r0, #%d, r1, asr #%d" % (rng.randrange(32), rng.randrange(1, 32)), ("r1",))
        case("ssat16_%d" % i, "ssat16 r0, #%d, r1" % rng.randrange(1, 17), ("r1",))
        case("usat16_%d" % i, "usat16 r0, #%d, r1" % rng.randrange(16), ("r1",))
        case("pkhbt_%d" % i, "pkhbt r0, r1, r2, lsl #%d" % rng.randrange(32), ("r1", "r2"))
        case("pkhtb_%d" % i, "pkhtb r0, r1, r2, asr #%d" % rng.randrange(1, 33), ("r1", "r2"))
    return cases


GROUPS = {"ls": ("loads and stores", ls_cases), "br": ("branches", br_cases),
          "mx": ("SP-relative ADD/SUB, ADR, MOVT, extend, bitfield, bit operations, multiply and divide", mx_cases),
          "sy": ("IT blocks, MRS and MSR (program status registers), barriers and hints", sy_cases),
          "dsp": ("DSP extension", dsp_cases)}


def program(group, title, cases, rng):
    out = ["""/*
 * Generated by tools/generate_insn_tests.py. Do not edit.
 *
 * Instruction test: %s. Each case sets the registers, runs one
 * instruction and prints its name, r0-r6, r8-r12, LR and, for a store, the
 * 16 bytes of its slot. The expected output is QEMU's (build.sh).
 */
    .syntax unified
    .cpu cortex-m7
    .thumb

#include "macros.inc"

    .equ SLOTS, 0x20000400
    .equ DUMP,  0x20000800

/* r7 = NZCV (bits 3..0), without changing the flags. */
.macro CAPTURE_FLAGS
    mov.w r7, #0
    bpl   4f
    orr.w r7, r7, #8
4:
    bne   5f
    orr.w r7, r7, #4
5:
    bcc   6f
    orr.w r7, r7, #2
6:
    bvc   7f
    orr.w r7, r7, #1
7:
.endm

/* Prints the 13 registers saved at DUMP, with \\flags the NZCV saved after
 * them, and with \\slot the 4 words at \\slot. */
.macro DUMP_LINE name, slot=0, flags=0
    ldr   r7, =USART3_BASE
    PUTS  \\name
    ldr   r0, =DUMP
    .irp k, 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48
    ldr   r2, [r0, #\\k]
    PRINT_HEX r2
    .endr
    .if \\flags
    ldr   r2, [r0, #52]
    PRINT_HEX r2
    .endif
    .if \\slot
    ldr   r0, =\\slot
    .irp k, 0, 4, 8, 12
    ldr   r2, [r0, #\\k]
    PRINT_HEX r2
    .endr
    .endif
    PUTC  '\\n'
    ldr   r7, =USART3_BASE
    POOL
.endm

    .section .vectors, "a"
    .word _estack
    .word reset_handler

    .text
    .thumb_func
    .global reset_handler
reset_handler:
    USART_INIT
    /* Clear 0x20000000..0x20000FFF (stack, slots, dump). */
    ldr   r1, =0x20000000
    ldr   r2, =0x20001000
    movs  r0, #0
1:
    str   r0, [r1, #0]
    adds  r1, #4
    cmp   r1, r2
    bne   1b
    POOL
""" % title]
    for c in cases:
        lines = ["", "    /* %s */" % c.name]
        values = {r: "0x%08X" % rnd(rng) for r in SET_REGS}
        for r, v in c.setup:
            values[r] = v
        for r in SET_REGS:
            if r in ("r0", "r1", "r2", "r3", "r4", "r5", "r6"):
                lines.append("    ldr   %s, =%s" % (r, values[r]))
            else:
                lines.append("    ldr.w %s, =%s" % (r, values[r]))
        lines.append("    b     8f")
        lines.append("    .ltorg")
        lines.append("8:")
        lines += ["    %s" % p for p in c.pre]
        lines.append("    %s" % c.insn)
        lines += ["    %s" % p for p in c.post]
        if c.flags:
            lines += ["    CAPTURE_FLAGS", "    push  {r7}"]
        lines.append("    ldr.w r7, =DUMP")
        lines.append("    stm.w r7, {r0-r6, r8-r12, lr}")
        if c.flags:
            lines += ["    pop   {r2}", "    str   r2, [r7, #52]"]
        lines.append("    DUMP_LINE %s, %s, %d" % (c.name, ("0x%08X" % c.slot) if c.slot else "0", 1 if c.flags else 0))
        out.append("\n".join(lines) + "\n")
    out.append("""
    PUTS  end
    PUTC  '\\n'
halt:
    wfi
    b     halt

    .ltorg
    .section .rodata
    .align 3
pattern_aligned:
pattern:
""")
    data = [rng.getrandbits(8) for _ in range(512)]
    for i in range(0, len(data), 16):
        out.append("    .byte " + ", ".join("0x%02X" % b for b in data[i:i + 16]) + "\n")
    return "".join(out)


def main():
    for group, (title, make) in GROUPS.items():
        rng = random.Random(SEED)
        cases = make(rng)
        with open(os.path.join(OUT, group + ".S"), "w") as f:
            f.write(program(group, title, cases, rng))
        print("%s: %d cases" % (group, len(cases)))


if __name__ == "__main__":
    main()

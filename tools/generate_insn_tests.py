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
    def __init__(self, name, insn, setup=(), slot=False, pre=(), post=()):
        self.name = name
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


GROUPS = {"ls": ("loads and stores", ls_cases)}


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

/* Prints the 13 registers saved at DUMP and, with \\slot, the 4 words at \\slot. */
.macro DUMP_LINE name, slot=0
    ldr   r7, =USART3_BASE
    PUTS  \\name
    ldr   r0, =DUMP
    .irp k, 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48
    ldr   r2, [r0, #\\k]
    PRINT_HEX r2
    .endr
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
        lines.append("    ldr.w r7, =DUMP")
        lines.append("    stm.w r7, {r0-r6, r8-r12, lr}")
        lines.append("    DUMP_LINE %s, %s" % (c.name, ("0x%08X" % c.slot) if c.slot else "0"))
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

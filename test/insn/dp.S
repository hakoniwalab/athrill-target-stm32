/*
 * Instruction test for Athrill's STM32 target: the data-processing
 * instructions in athrill-arm-common (ADD, SUB, CMP, AND, MOV immediate and
 * register, MOV register-shifted register), every Thumb encoding of each.
 *
 * Each case prints "<name> <result> [<NZCV>]" on USART3; the expected lines
 * are worked out by hand from the Armv7-M pseudo-code and checked by CTest.
 * Before a flag-setting case, SETC or CLRC fixes C and V, and "movs r5, #0"
 * (which sets only N and Z) clears the flag record.
 */
    .syntax unified
    .cpu cortex-m7
    .thumb

#include "macros.inc"

    .section .vectors, "a"
    .word _estack
    .word reset_handler

    .text
    .thumb_func
    .global reset_handler
reset_handler:
    USART_INIT

    /* ---- 16-bit, immediate ---- */

    /* SUBS <Rd>, <Rn>, #<imm3> (T1): 5 - 3 = 2, C (no borrow). */
    movs  r1, #5
    movs  r5, #0
    subs  r0, r1, #3
    SHOWF sub_t1, r0

    /* SUBS <Rdn>, #<imm8> (T2): 5 - 6 = -1, N, borrow. */
    movs  r0, #5
    movs  r5, #0
    subs  r0, #6
    SHOWF sub_t2, r0

    /* CMP <Rn>, #<imm8> (T1): 5 - 5: Z and C. */
    movs  r1, #5
    movs  r5, #0
    cmp   r1, #5
    SHOWC cmp_t1

    /* MOVS <Rd>, #<imm8> (T1): N and Z from the value, C and V unchanged (SETC). */
    SETC
    movs  r5, #0
    movs  r0, #0x80
    SHOWF mov_t1, r0

    /* ---- 16-bit, register ---- */

    /* ADDS <Rd>, <Rn>, <Rm> (T1): 0x7FFFFFFF + 1: N and V. */
    LDC   r1, 0x7FFFFFFF
    movs  r2, #1
    movs  r5, #0
    adds  r0, r1, r2
    SHOWF add_reg_t1, r0

    /* MOV <Rd>, <Rm> (T1) to and from r8, ADD <Rdn>, <Rm> (T2) on r8: 0x11 + 0x22. */
    movs  r1, #0x11
    mov   r8, r1
    movs  r2, #0x22
    add   r8, r2
    mov   r0, r8
    SHOW  add_reg_t2, r0

    /* ANDS <Rdn>, <Rm> (T1): C and V unchanged (SETC). */
    LDC   r0, 0xF0F0F0F0
    LDC   r1, 0x0FF00FF0
    SETC
    movs  r5, #0
    ands  r0, r1
    SHOWF and_reg_t1, r0

    /* CMP <Rn>, <Rm> (T1): 3 - 5: N, borrow. */
    movs  r1, #3
    movs  r2, #5
    movs  r5, #0
    cmp   r1, r2
    SHOWC cmp_reg_t1

    /* CMP <Rn>, <Rm> (T2, a high register): r8 = 5, r1 = 5: Z and C. */
    movs  r1, #5
    mov   r8, r1
    movs  r5, #0
    cmp   r8, r1
    SHOWC cmp_reg_t2

    /* LSRS <Rd>, <Rm>, #4 (MOV T2): 0x18 >> 4 = 1, C = bit 3. */
    movs  r1, #0x18
    SETC
    movs  r5, #0
    lsrs  r0, r1, #4
    SHOWF mov_reg_t2, r0

    /* LSLS <Rdm>, <Rs> (T1): 3 << 31 = 0x80000000, C = bit 1. */
    movs  r0, #3
    movs  r2, #31
    movs  r5, #0
    lsls  r0, r2
    SHOWF lsl_reg_t1, r0

    /* LSRS <Rdm>, <Rs> (T1): 0x80000000 >> 32 = 0, C = bit 31. */
    LDC   r0, 0x80000000
    movs  r2, #32
    movs  r5, #0
    lsrs  r0, r2
    SHOWF lsr_reg_t1, r0

    /* ASRS <Rdm>, <Rs> (T1): R[s]<7:0> of 0x101 is 1: 0xC0000000, C = bit 0 = 0. */
    LDC   r0, 0x80000000
    LDC   r2, 0x101
    SETC
    movs  r5, #0
    asrs  r0, r2
    SHOWF asr_reg_t1, r0

    /* RORS <Rdm>, <Rs> (T1): 1 ror 1 = 0x80000000, C = bit 31 of the result. */
    movs  r0, #1
    movs  r2, #1
    movs  r5, #0
    rors  r0, r2
    SHOWF ror_reg_t1, r0

    /* ---- 32-bit, immediate ---- */

    /* SUB.W <Rd>, <Rn>, #<const> (T3): const = 0x00FF00FF. */
    LDC   r1, 0x12345678
    sub.w r0, r1, #0x00FF00FF
    SHOW  sub_t3, r0

    /* SUBS.W (T3): 0x100 - 0x100 = 0: Z and C. */
    LDC   r1, 0x100
    movs  r5, #0
    subs.w r0, r1, #0x100
    SHOWF subs_t3, r0

    /* SUBW <Rd>, <Rn>, #<imm12> (T4): 0x1000 - 0xFFF. */
    LDC   r1, 0x1000
    subw  r0, r1, #0xFFF
    SHOW  sub_t4, r0

    /* CMP.W <Rn>, #<const> (T2): 0xFFFF - 0x10000 < 0: N, borrow. */
    LDC   r1, 0xFFFF
    movs  r5, #0
    cmp.w r1, #0x10000
    SHOWC cmp_t2

    /* AND.W <Rd>, <Rn>, #<const> (T1): const = 0xFF00FF00. */
    LDC   r1, 0x12345678
    and.w r0, r1, #0xFF00FF00
    SHOW  and_t1, r0

    /* ANDS.W (T1), rotated constant 0x80000000: C = bit 31 of the constant (CLRC before). */
    LDC   r1, 0x80000001
    CLRC
    movs  r5, #0
    ands.w r0, r1, #0x80000000
    SHOWF ands_t1, r0

    /* ANDS.W (T1), replicated constant 0x00FF00FF: C unchanged (CLRC before). */
    LDC   r1, 0x12345678
    CLRC
    movs  r5, #0
    ands.w r0, r1, #0x00FF00FF
    SHOWF ands_t1_rep, r0

    /* MOV.W <Rd>, #<const> (T2): const = 0xAB00AB00. */
    mov.w r0, #0xAB00AB00
    SHOW  mov_t2, r0

    /* MOVS.W (T2), rotated constant 0x40000000: C = its bit 31 = 0 (SETC before). */
    SETC
    movs  r5, #0
    movs.w r0, #0x40000000
    SHOWF movs_t2, r0

    /* MOVW <Rd>, #<imm16> (T3). */
    movw  r0, #0xBEEF
    SHOW  mov_t3, r0

    /* ---- 32-bit, register ---- */

    /* ADD.W <Rd>, <Rn>, <Rm>, LSL #8 (T3): 1 + (0x12 << 8). */
    movs  r1, #1
    movs  r2, #0x12
    add.w r0, r1, r2, lsl #8
    SHOW  add_reg_t3, r0

    /* ADDS.W (T3), ASR #1: 1 + (0xFFFFFFFE ASR 1) = 0: Z and C. */
    movs  r1, #1
    LDC   r2, 0xFFFFFFFE
    movs  r5, #0
    adds.w r0, r1, r2, asr #1
    SHOWF adds_reg_t3, r0

    /* ADD.W (T3), RRX with C = 1: 1 + (1:2<31:1>) = 0x80000002. */
    movs  r1, #1
    movs  r2, #2
    SETC
    add.w r0, r1, r2, rrx
    SHOW  add_reg_t3_rrx, r0

    /* ADDS.W (T3), RRX with C = 0: 0xFFFFFFFF + (0:2<31:1>) = 0: Z and C. */
    LDC   r1, 0xFFFFFFFF
    movs  r2, #2
    CLRC
    movs  r5, #0
    adds.w r0, r1, r2, rrx
    SHOWF adds_reg_t3_rrx, r0

    /* CMP.W <Rn>, <Rm>, LSL #1 (T3): 8 - 8: Z and C. */
    movs  r1, #8
    movs  r2, #4
    movs  r5, #0
    cmp.w r1, r2, lsl #1
    SHOWC cmp_reg_t3

    /* CMP.W (T3), RRX with C = 1: 0 - 0x80000000: N, borrow, V. */
    movs  r1, #0
    movs  r2, #0
    SETC
    movs  r5, #0
    cmp.w r1, r2, rrx
    SHOWC cmp_reg_t3_rrx

    /* AND.W <Rd>, <Rn>, <Rm>, LSR #4 (T2). */
    LDC   r1, 0xFFFFFFFF
    LDC   r2, 0xABCD0000
    and.w r0, r1, r2, lsr #4
    SHOW  and_reg_t2, r0

    /* ANDS.W (T2), LSL #1: the shift's carry (bit 31 of 0x80000000) with result 0 (CLRC before). */
    LDC   r1, 0xFFFFFFFF
    LDC   r2, 0x80000000
    CLRC
    movs  r5, #0
    ands.w r0, r1, r2, lsl #1
    SHOWF ands_reg_t2, r0

    /* AND.W (T2), RRX with C = 1: 0xFFFFFFFF AND (1:0<31:1>). */
    LDC   r1, 0xFFFFFFFF
    movs  r2, #0
    SETC
    and.w r0, r1, r2, rrx
    SHOW  and_reg_t2_rrx, r0

    /* ANDS.W (T2), RRX with C = 0: shifted 0, carry = bit 0 of 1. */
    LDC   r1, 0xFFFFFFFF
    movs  r2, #1
    CLRC
    movs  r5, #0
    ands.w r0, r1, r2, rrx
    SHOWF ands_reg_t2_rrx, r0

    /* MOV.W <Rd>, <Rm> (T3). */
    LDC   r1, 0x12345678
    mov.w r0, r1
    SHOW  mov_reg_t3, r0

    /* LSLS.W <Rd>, <Rm>, #4 (MOVS T3): C = bit 28. */
    LDC   r1, 0xF0000001
    CLRC
    movs  r5, #0
    lsls.w r0, r1, #4
    SHOWF movs_reg_t3, r0

    /* RRX <Rd>, <Rm> (MOV T3, RRX) with C = 1. */
    movs  r1, #2
    SETC
    rrx   r0, r1
    SHOW  mov_reg_t3_rrx, r0

    /* RRXS <Rd>, <Rm> (MOVS T3, RRX) with C = 0: 0, C = bit 0 of 1. */
    movs  r1, #1
    CLRC
    movs  r5, #0
    rrxs  r0, r1
    SHOWF movs_reg_t3_rrx, r0

    /* LSL.W <Rd>, <Rm>, <Rs> (T2): shift by 33 gives 0. */
    movs  r1, #1
    movs  r2, #33
    lsl.w r0, r1, r2
    SHOW  mov_rsr_t2, r0

    /* ASRS.W <Rd>, <Rm>, <Rs> (T2): 0x80000000 ASR 31 = -1, C = bit 30 = 0 (SETC before). */
    LDC   r1, 0x80000000
    movs  r2, #31
    SETC
    movs  r5, #0
    asrs.w r0, r1, r2
    SHOWF movs_rsr_t2, r0

    PUTS  end
    PUTC  '\n'
halt:
    wfi
    b     halt

    .ltorg

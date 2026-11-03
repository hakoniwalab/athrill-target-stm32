/*
 * Instruction test for Athrill's STM32 target: ADD (immediate), LDR
 * (immediate) and B in every Thumb encoding Armv7-M has (16-bit T1/T2,
 * 32-bit T3/T4), executed through athrill-arm-common's pseudo-code.
 *
 * Each case prints "<name> <result> [<NZCV>]" on USART3; the expected lines
 * are worked out by hand from the Armv7-M pseudo-code and checked by CTest
 * (CMakeLists.txt). Registers: r7 = USART3, r5 = the NZCV flags of the
 * case (bit 3 = N ... bit 0 = V), r0/r2 = results; printing uses r1, r3,
 * r4 and r6.
 */
    .syntax unified
    .cpu cortex-m7
    .thumb

#include "macros.inc"

/* Loads constant \index of the table consts (the assembler would turn
 * "ldr rN, =value" into MOV/MVN for these values). */
.macro LOAD_CONST reg, index
    ldr   \reg, =consts
    ldr   \reg, [\reg, #(\index * 4)]
.endm

    .section .vectors, "a"
    .word _estack
    .word reset_handler

    .text
    .thumb_func
    .global reset_handler
reset_handler:
    USART_INIT

    POOL

    /* ADDS <Rd>, <Rn>, #<imm3> (T1): 0xFFFFFFFF + 1 = 0, Z and C. */
    movs  r5, #0
    LOAD_CONST r1, 0
    adds  r0, r1, #1
    RECORD_FLAGS
    PUTS  add_t1
    PRINT_HEX r0
    PRINT_FLAGS
    PUTC  '\n'

    POOL

    /* ADDS <Rdn>, #<imm8> (T2): 0x7FFFFFFF + 1 = 0x80000000, N and V. */
    movs  r5, #0
    LOAD_CONST r0, 1
    adds  r0, #1
    RECORD_FLAGS
    PUTS  add_t2
    PRINT_HEX r0
    PRINT_FLAGS
    PUTC  '\n'

    POOL

    /* ADD.W <Rd>, <Rn>, #<const> (T3): const = ThumbExpandImm(0x1AB) = 0x00AB00AB. */
    ldr   r1, =0x12345678
    add.w r0, r1, #0x00AB00AB
    PUTS  add_t3
    PRINT_HEX r0
    PUTC  '\n'

    POOL

    /* ADDS.W (T3): const 0x100 (a rotated form); 0xFFFFFF00 + 0x100 = 0, Z and C. */
    movs  r5, #0
    LOAD_CONST r1, 2
    adds.w r0, r1, #0x100
    RECORD_FLAGS
    PUTS  adds_t3
    PRINT_HEX r0
    PRINT_FLAGS
    PUTC  '\n'

    POOL

    /* ADDW <Rd>, <Rn>, #<imm12> (T4): 0x12345678 + 0xFFF. */
    ldr   r1, =0x12345678
    addw  r0, r1, #0xFFF
    PUTS  add_t4
    PRINT_HEX r0
    PUTC  '\n'

    POOL

    /* LDR <Rt>, [<Rn>, #imm5:'00'] (T1). */
    ldr   r1, =data
    ldr   r0, [r1, #4]
    PUTS  ldr_t1
    PRINT_HEX r0
    PUTC  '\n'

    POOL

    /* LDR <Rt>, [SP, #imm8:'00'] (T2): SP is _estack; the word at SP + 8 is written first. */
    ldr   r1, =_estack
    LOAD_CONST r2, 3
    str   r2, [r1, #8]
    ldr   r0, [sp, #8]
    PUTS  ldr_t2
    PRINT_HEX r0
    PUTC  '\n'

    POOL

    /* LDR.W <Rt>, [<Rn>, #imm12] (T3). */
    ldr   r1, =data
    ldr.w r0, [r1, #12]
    PUTS  ldr_t3
    PRINT_HEX r0
    PUTC  '\n'

    POOL

    /* LDR <Rt>, [<Rn>, #-imm8] (T4, offset): data + 8 - 4. */
    ldr   r1, =data + 8
    ldr   r0, [r1, #-4]
    PUTS  ldr_t4_off
    PRINT_HEX r0
    PUTC  '\n'

    POOL

    /* LDR <Rt>, [<Rn>, #imm8]! (T4, pre-indexed): loads data + 4, Rn = data + 4;
     * then [Rn, #4] is data + 8. */
    ldr   r1, =data
    ldr   r2, [r1, #4]!
    ldr   r0, [r1, #4]
    PUTS  ldr_t4_pre
    PRINT_HEX r2
    PRINT_HEX r0
    PUTC  '\n'

    POOL

    /* LDR <Rt>, [<Rn>], #imm8 (T4, post-indexed): loads data, Rn = data + 4;
     * then [Rn, #4] is data + 8. */
    ldr   r1, =data
    ldr   r2, [r1], #4
    ldr   r0, [r1, #4]
    PUTS  ldr_t4_post
    PRINT_HEX r2
    PRINT_HEX r0
    PUTC  '\n'

    POOL

    /* LDR <Rt>, [<Rn>, #-imm8]! (T4, pre-indexed, subtract): data + 12 - 8. */
    ldr   r1, =data + 12
    ldr   r2, [r1, #-8]!
    ldr   r0, [r1, #0]
    PUTS  ldr_t4_sub
    PRINT_HEX r2
    PRINT_HEX r0
    PUTC  '\n'

    /* B<c> (T1): BEQ taken with Z set, BNE not taken. */
    movs  r0, #0
    beq   b_t1_taken
    b     b_t1_ng
b_t1_taken:
    bne   b_t1_ng
    PUTS  b_t1
    PUTC  ' '
    PUTS  ok
    PUTC  '\n'
    b     b_t2_test
b_t1_ng:
    PUTS  b_t1
    PUTC  ' '
    PUTS  NG
    PUTC  '\n'

    /* B (T2): forward over a failure, and backward. */
b_t2_test:
    b     b_t2_forward
    PUTS  b_t2
    PUTC  ' '
    PUTS  NG
    PUTC  '\n'
b_t2_back:
    PUTS  b_t2
    PUTC  ' '
    PUTS  ok
    PUTC  '\n'
    b     b_t3_test
b_t2_forward:
    b     b_t2_back

    /* B<c>.W (T3): BEQ.W taken with Z set (forward), BNE.W not taken, BEQ.W backward. */
b_t3_test:
    movs  r0, #0
    beq.w b_t3_forward
    PUTS  b_t3
    PUTC  ' '
    PUTS  NG
    PUTC  '\n'
b_t3_back:
    bne.w b_t3_ng
    PUTS  b_t3
    PUTC  ' '
    PUTS  ok
    PUTC  '\n'
    b     b_t4_test
b_t3_forward:
    beq.w b_t3_back
b_t3_ng:
    PUTS  b_t3
    PUTC  ' '
    PUTS  NG
    PUTC  '\n'

    /* B.W (T4): forward over a failure, and backward. */
b_t4_test:
    b.w   b_t4_forward
    PUTS  b_t4
    PUTC  ' '
    PUTS  NG
    PUTC  '\n'
b_t4_back:
    PUTS  b_t4
    PUTC  ' '
    PUTS  ok
    PUTC  '\n'
    b     done
b_t4_forward:
    b.w   b_t4_back

done:
    PUTS  end
    PUTC  '\n'
halt:
    wfi
    b     halt

    .ltorg
    .align 2
data:
    .word 0x11111111, 0x22222222, 0x33333333, 0x44444444
consts:
    .word 0xFFFFFFFF, 0x7FFFFFFF, 0xFFFFFF00, 0x55555555

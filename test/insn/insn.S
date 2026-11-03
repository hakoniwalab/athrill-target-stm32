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

    .equ USART3_BASE, 0x40004800
    .equ USART_ISR,   0x1C
    .equ USART_TDR,   0x28

/* Writes the character in \reg (r1-r6) to USART3. */
.macro PUTR reg
1:
    ldr   r3, [r7, #USART_ISR]
    lsls  r3, r3, #24               /* TXE (bit 7) into N */
    bpl   1b
    str   \reg, [r7, #USART_TDR]
.endm

.macro PUTC chr
    movs  r1, #\chr
    PUTR  r1
.endm

.macro PUTS text
    .pushsection .rodata.strings, "a"
str\@:
    .asciz "\text"
    .popsection
    ldr   r6, =str\@
puts\@:
    ldrb  r1, [r6, #0]
    cmp   r1, #0
    beq   puts_end\@
    PUTR  r1
    adds  r6, #1
    b     puts\@
puts_end\@:
.endm

/* Prints the top \digits hex digits of r4. */
.macro PRINT_R4 digits
    movs  r6, #\digits
2:
    lsrs  r1, r4, #28
    cmp   r1, #10
    bcc   3f
    adds  r1, #7
3:
    adds  r1, #48
    PUTR  r1
    lsls  r4, r4, #4
    subs  r6, #1
    bne   2b
.endm

.macro PRINT_HEX reg
    PUTC  ' '
    adds  r4, \reg, #0
    PRINT_R4 8
.endm

/* r5 must be 0 before the instruction under test (movs r5, #0 sets only N, Z). */
.macro RECORD_FLAGS
    bpl   4f
    addw  r5, r5, #8
4:
    bne   5f
    addw  r5, r5, #4
5:
    bcc   6f
    addw  r5, r5, #2
6:
    bvc   7f
    addw  r5, r5, #1
7:
.endm

/* A literal pool within reach of the 16-bit LDR (literal) of the next case. */
.macro POOL
    b     8f
    .ltorg
8:
.endm

/* Loads constant \index of the table consts (the assembler would turn
 * "ldr rN, =value" into MOV/MVN for these values). */
.macro LOAD_CONST reg, index
    ldr   \reg, =consts
    ldr   \reg, [\reg, #(\index * 4)]
.endm

.macro PRINT_FLAGS
    PUTC  ' '
    lsls  r4, r5, #28
    PRINT_R4 1
.endm

    .section .vectors, "a"
    .word _estack
    .word reset_handler

    .text
    .thumb_func
    .global reset_handler
reset_handler:
    ldr   r7, =USART3_BASE

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

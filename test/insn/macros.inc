/*
 * Output and flag macros of the instruction tests (test/insn). Registers:
 * r7 = USART3, r5 = the NZCV flags of a case (bit 3 = N ... bit 0 = V);
 * printing uses r1, r3, r4 and r6.
 */
    .equ USART3_BASE, 0x40004800
#ifdef USART_STM32F4
/* STM32F4's USART (QEMU's netduinoplus2, the reference run): SR and DR. */
    .equ USART_ISR,   0x00
    .equ USART_TDR,   0x04
#else
/* STM32F7's USART: ISR and TDR. */
    .equ USART_ISR,   0x1C
    .equ USART_TDR,   0x28
#endif

/* Sets r7 to USART3. QEMU's STM32F4 USART sends only when enabled
 * (CR1: UE, TE); the STM32 target sends without it. Both builds have the
 * same instructions here (a NOP for the store), so that every address of
 * the program is the same in both and addresses can be printed. */
.macro USART_INIT
    ldr   r7, =USART3_BASE
    ldr   r1, =0x2008
#ifdef USART_STM32F4
    str   r1, [r7, #0x0C]
#else
    nop
#endif
.endm

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


.macro PRINT_FLAGS
    PUTC  ' '
    lsls  r4, r5, #28
    PRINT_R4 1
.endm

/* Loads a 32-bit constant from a table (the assembler would turn
 * "ldr rN, =value" into MOV/MVN for some values). Needs a POOL in reach. */
.macro LDC reg, value
    .pushsection .rodata.consts, "a"
    .align 2
const\@:
    .word \value
    .popsection
    ldr   \reg, =const\@
    ldr   \reg, [\reg, #0]
.endm

/* Known C and V before a case: SETC gives C = 1, V = 0; CLRC gives C = 0, V = 0. */
.macro SETC
    movs  r3, #0
    cmp   r3, #0
.endm

.macro CLRC
    movs  r3, #0
    cmp   r3, #1
.endm

.macro SHOW name, reg
    PUTS  \name
    PRINT_HEX \reg
    PUTC  '\n'
    POOL
.endm

.macro SHOWC name
    RECORD_FLAGS
    PUTS  \name
    PRINT_FLAGS
    PUTC  '\n'
    POOL
.endm

.macro SHOWF name, reg
    RECORD_FLAGS
    PUTS  \name
    PRINT_HEX \reg
    PRINT_FLAGS
    PUTC  '\n'
    POOL
.endm

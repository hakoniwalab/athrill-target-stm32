/*
 * Output and flag macros of the instruction tests (test/insn). Registers:
 * r7 = USART3, r5 = the NZCV flags of a case (bit 3 = N ... bit 0 = V);
 * printing uses r1, r3, r4 and r6.
 */
#ifdef TARGET_AN500
/* QEMU's mps2-an500 (Cortex-M7 with FPv5-D16, the reference run of the
 * floating-point tests): UART0, a CMSDK APB UART (DATA, STATE: TXFULL). */
    .equ USART3_BASE, 0x40004000
    .equ USART_ISR,   0x04
    .equ USART_TDR,   0x00
#else
    .equ USART3_BASE, 0x40004800
#endif
#if defined(TARGET_AN500)
#elif defined(USART_STM32F4)
/* STM32F4's USART (QEMU's netduinoplus2, the reference run): SR and DR. */
    .equ USART_ISR,   0x00
    .equ USART_TDR,   0x04
#else
/* STM32F7's USART: ISR and TDR. */
    .equ USART_ISR,   0x1C
    .equ USART_TDR,   0x28
#endif

/* Sets r7 to USART3 and enables its transmitter (CR1: UE, TE; at 0x0C on
 * the STM32F4, at 0x00 on the STM32F7). Both builds have the same
 * instructions here, so that every address of the program is the same in
 * both and addresses can be printed. */
.macro USART_INIT
#ifdef TARGET_AN500
    /* The CMSDK UART: BAUDDIV, then CTRL.TX_EN. The FPU: CPACR CP10 and
     * CP11 full access (Athrill does not model CPACR: its FPU is enabled). */
    ldr   r7, =USART3_BASE
    movs  r1, #16
    str   r1, [r7, #0x10]
    movs  r1, #1
    str   r1, [r7, #0x08]
    ldr   r0, =0xE000ED88
    ldr   r1, [r0]
    orr   r1, r1, #(0xF << 20)
    str   r1, [r0]
    dsb
    isb
#else
    ldr   r7, =USART3_BASE
#ifdef USART_STM32F4
    ldr   r1, =0x2008
    str   r1, [r7, #0x0C]
#else
    ldr   r1, =0x0009
    str   r1, [r7, #0x00]
#endif
#endif
.endm

/* Writes the character in \reg (r1-r6) to USART3. */
.macro PUTR reg
1:
    ldr   r3, [r7, #USART_ISR]
#ifdef TARGET_AN500
    lsls  r3, r3, #31               /* TXFULL (bit 0) into N */
    bmi   1b
#else
    lsls  r3, r3, #24               /* TXE (bit 7) into N */
    bpl   1b
#endif
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

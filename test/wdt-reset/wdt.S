/*
 * Stage C firmware for the watchdog reset proof (#138). Prints "BOOT\n" on
 * USART3, then spins. Built without KICK it never touches the watchdog, so the
 * watchdog times out and resets the MCU (BOOT prints again after the reboot).
 * Built with -DKICK it writes to the watchdog every iteration, so it never
 * fires (BOOT prints once). 16-bit Thumb only (the bootstrap decoder subset).
 */
    .syntax unified
    .cpu cortex-m7
    .thumb

    .equ USART3_BASE, 0x40004800
    .equ USART_ISR,   0x1C
    .equ USART_TDR,   0x28
    .equ WDT_BASE,    0x40003000
    .equ BOOT_COUNT,  0x20000000

    .section .vectors, "a"
    .word _estack
    .word reset_handler

    .text
    .thumb_func
    .global reset_handler
reset_handler:
    ldr   r6, =BOOT_COUNT
    ldr   r7, [r6, #0]
    adds  r7, #1
    str   r7, [r6, #0]
    ldr   r0, =USART3_BASE
    movs  r1, #0x9                  /* CR1: UE, TE */
    str   r1, [r0, #0]
    adr   r1, message
next_char:
    ldrb  r2, [r1, #0]
    cmp   r2, #0
    beq   loop
wait_txe:
    ldr   r3, [r0, #USART_ISR]
    lsls  r3, r3, #24               /* TXE (bit 7) into N */
    bpl   wait_txe
    str   r2, [r0, #USART_TDR]
    adds  r1, #1
    b     next_char
loop:
    ldr   r4, =WDT_BASE
    movs  r5, #1
spin:
#ifdef KICK
    str   r5, [r4, #0]              /* any write reloads the watchdog */
#else
    cmp   r7, #2                    /* after the one expected reboot, keep it alive */
    blo   no_kick
    str   r5, [r4, #0]
no_kick:
#endif
    b     spin

    .align 2
message:
    .asciz "BOOT\n"
    .align 2
    .ltorg

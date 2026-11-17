/*
 * Stage 3 firmware: the real STM32 IWDG. Prints "BOOT" on USART3, sets up the
 * IWDG (unlock 0x5555, PR=0 /4, RLR=16, start 0xCCCC), then spins. Without KICK
 * it never reloads, so the IWDG resets the MCU; the firmware re-arms on each
 * reboot, so BOOT prints repeatedly. With -DKICK it reloads (0xAAAA) every
 * iteration and never resets (BOOT once). 16-bit Thumb only.
 */
    .syntax unified
    .cpu cortex-m7
    .thumb

    .equ USART3_BASE, 0x40004800
    .equ USART_ISR,   0x1C
    .equ USART_TDR,   0x28

    .section .vectors, "a"
    .word _estack
    .word reset_handler

    .text
    .thumb_func
    .global reset_handler
reset_handler:
    ldr   r0, =USART3_BASE
    movs  r1, #0x9                  /* CR1: UE, TE */
    str   r1, [r0, #0]
    adr   r1, message
next_char:
    ldrb  r2, [r1, #0]
    cmp   r2, #0
    beq   setup
wait_txe:
    ldr   r3, [r0, #USART_ISR]
    lsls  r3, r3, #24               /* TXE (bit 7) into N */
    bpl   wait_txe
    str   r2, [r0, #USART_TDR]
    adds  r1, #1
    b     next_char
setup:
    ldr   r0, =0x40003000           /* IWDG base */
    ldr   r1, =0x5555
    str   r1, [r0, #0]              /* KR: enable PR/RLR write access */
    movs  r1, #0
    str   r1, [r0, #4]              /* PR = 0 (/4) */
    movs  r1, #16
    str   r1, [r0, #8]              /* RLR = 16 */
    ldr   r1, =0xCCCC
    str   r1, [r0, #0]              /* KR: start */
    ldr   r2, =0xAAAA
spin:
#ifdef KICK
    str   r2, [r0, #0]             /* KR: reload (kick) */
#endif
    b     spin

    .align 2
message:
    .asciz "BOOT\n"
    .align 2
    .ltorg

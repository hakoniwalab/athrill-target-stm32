/*
 * Bare-metal hello world for Athrill's STM32 target: writes a message to
 * USART3 (polling TXE in ISR, writing TDR) and stops in WFI.
 * 16-bit Thumb instructions only (the bootstrap decoder's subset).
 */
    .syntax unified
    .cpu cortex-m7
    .thumb

    .equ USART3_BASE, 0x40004800
    .equ USART_ISR,   0x1C
    .equ USART_TDR,   0x28

    .section .vectors, "a"
    .word _estack                   /* initial main stack pointer */
    .word reset_handler             /* reset vector (Thumb bit set by the assembler) */

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
    beq   done
wait_txe:
    ldr   r3, [r0, #USART_ISR]
    lsls  r3, r3, #24               /* TXE (bit 7) into N */
    bpl   wait_txe
    str   r2, [r0, #USART_TDR]
    adds  r1, #1
    b     next_char
done:
    wfi
    b     done

    .align 2
message:
    .asciz "Hello, Athrill STM32!\n"
    .align 2
    .ltorg

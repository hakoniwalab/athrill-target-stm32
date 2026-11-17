/*
 * Stage 3 firmware: the real STM32 IWDG. Prints "BOOT" on USART3, sets up the
 * IWDG (unlock 0x5555, PR=0 /4, RLR=16, start 0xCCCC), then spins. Without KICK
 * it never reloads, so the IWDG resets the MCU once. The second boot checks
 * that the IWDG model returned to its reset value, reports RESET_OK, and does
 * not start it again. With -DKICK it reloads (0xAAAA) every iteration and
 * never resets (BOOT once). SRAM keeps a boot count across the system reset.
 */
    .syntax unified
    .cpu cortex-m7
    .thumb

    .equ USART3_BASE, 0x40004800
    .equ USART_ISR,   0x1C
    .equ USART_TDR,   0x28
    .equ IWDG_BASE,   0x40003000
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
    bl    print
#ifndef KICK
	cmp   r7, #2
	bhs   verify_reset
#endif
setup:
	ldr   r4, =IWDG_BASE
	ldr   r1, =0x5555
	str   r1, [r4, #0]              /* KR: enable PR/RLR write access */
	movs  r1, #0
	str   r1, [r4, #4]              /* PR = 0 (/4) */
	movs  r1, #16
	str   r1, [r4, #8]              /* RLR = 16 */
	ldr   r1, =0xCCCC
	str   r1, [r4, #0]              /* KR: start */
	ldr   r5, =0xAAAA
#ifdef KICK
	movs  r6, #255
	movs  r7, #255
#endif
spin:
#ifdef KICK
	str   r5, [r4, #0]              /* KR: reload (kick) */
	subs  r6, #1
	bne   spin
	movs  r6, #255
	subs  r7, #1
	bne   spin
	adr   r1, alive_message
	bl    print
	movs  r7, #255
#endif
	b     spin

verify_reset:
	ldr   r0, =IWDG_BASE
	ldr   r1, [r0, #8]              /* RLR returns to 0xFFF on peripheral reset */
	ldr   r2, =0x0FFF
	cmp   r1, r2
	bne   reset_bad
	adr   r1, reset_ok
	b     report_reset
reset_bad:
	adr   r1, reset_bad_message
report_reset:
	bl    print
	b     .

print:
	ldr   r0, =USART3_BASE
next_char:
    ldrb  r2, [r1, #0]
    cmp   r2, #0
	beq   print_done
wait_txe:
    ldr   r3, [r0, #USART_ISR]
    lsls  r3, r3, #24               /* TXE (bit 7) into N */
    bpl   wait_txe
    str   r2, [r0, #USART_TDR]
    adds  r1, #1
    b     next_char
	print_done:
	bx    lr

    .align 2
message:
    .asciz "BOOT\n"
reset_ok:
	.asciz "RESET_OK\n"
reset_bad_message:
	.asciz "RESET_BAD\n"
alive_message:
	.asciz "ALIVE\n"
    .align 2
    .ltorg

/*
 * Console input test for Athrill's STM32 target (test/console/run.py):
 * prints "READY" on USART3 (the console UART), then echoes every received
 * character as "<c>" (polling RXNE). Characters typed in Athrill's debugger
 * ("serial 0 AB") reach USART3 through the console UART model.
 */
    .syntax unified
    .cpu cortex-m7
    .thumb

    .equ USART3_BASE, 0x40004800
    .equ USART_CR1,   0x00
    .equ USART_ISR,   0x1C
    .equ USART_RDR,   0x24
    .equ USART_TDR,   0x28
    .equ ISR_RXNE,    (1 << 5)
    .equ ISR_TXE,     (1 << 7)

    .section .vectors, "a"
    .word _estack
    .word reset_handler

    .text
    .thumb_func
    .global reset_handler
reset_handler:
    ldr   r0, =USART3_BASE
    /* CR1: UE, RE, TE and RXNEIE. Athrill's UART model hands received data
     * to RDR only when RXNEIE or DMAR is set (a driver that neither takes
     * the interrupt nor uses DMA is not modelled); the NVIC line stays
     * disabled, so the program polls RXNE and no interrupt is taken. */
    movs  r1, #0x2D
    str   r1, [r0, #USART_CR1]
    adr   r1, ready
    bl    puts
receive:
    ldr   r3, [r0, #USART_ISR]
    tst   r3, #ISR_RXNE
    beq   receive
    ldr   r4, [r0, #USART_RDR]
    movs  r2, #'<'
    bl    putc
    mov   r2, r4
    bl    putc
    movs  r2, #'>'
    bl    putc
    b     receive

    .thumb_func
puts:                               /* r1: string; uses r2, r3 */
    push  {lr}
puts_next:
    ldrb  r2, [r1], #1
    cmp   r2, #0
    beq   puts_done
    bl    putc
    b     puts_next
puts_done:
    pop   {pc}

    .thumb_func
putc:                               /* r2: character; uses r3 */
    ldr   r3, [r0, #USART_ISR]
    tst   r3, #ISR_TXE
    beq   putc
    str   r2, [r0, #USART_TDR]
    bx    lr

    .align 2
ready:
    .asciz "READY\n"
    .align 2
    .ltorg

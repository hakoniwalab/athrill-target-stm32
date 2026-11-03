/*
 * Exception test: the M profile's exception model against QEMU
 * (netduinoplus2, a Cortex-M4 with the same memory map as the STM32
 * target, so addresses are printed too).
 *
 * All vectors lead to one handler that logs, per exception taken: IPSR,
 * EXC_RETURN, the frame's address, the stacked R0, PC and xPSR, CONTROL and
 * CFSR. A scenario puts in ACTIONS[n] a function the handler calls for
 * exception n (r0 = the frame); a nonzero result replaces EXC_RETURN. After
 * each scenario the thread prints the log and what it checks.
 *
 * Written by hand: each scenario is a situation of the exception model
 * (preemption, masking, tail-chaining, the process stack, faults, the
 * floating-point context), not an instruction with random operands.
 */
    .syntax unified
    .cpu cortex-m4
    .fpu fpv4-sp-d16
    .thumb

#include "macros.inc"

    .equ ACTIONS,   0x20000000      /* 32 words: an action per exception number */
    .equ LOG_COUNT, 0x200000F0
    .equ SKIP,      0x200000F4      /* bytes a fault handler skips (0: none) */
    .equ LOG,       0x20000100      /* 16 entries of 8 words */
    .equ PSP_TOP,   0x20000800
    .equ VAR,       0x20000900      /* scratch words of the scenarios */

    .equ SCS_ICSR,  0xE000ED04
    .equ SCS_VTOR,  0xE000ED08
    .equ SCS_AIRCR, 0xE000ED0C
    .equ SCS_CCR,   0xE000ED14
    .equ SCS_SHPR2, 0xE000ED1C
    .equ SCS_SHPR3, 0xE000ED20
    .equ SCS_SHCSR, 0xE000ED24
    .equ SCS_CFSR,  0xE000ED28
    .equ SCS_HFSR,  0xE000ED2C
    .equ SCS_CPACR, 0xE000ED88
    .equ SCS_FPCCR, 0xE000EF34
    .equ SCS_FPCAR, 0xE000EF38
    .equ NVIC_ISER, 0xE000E100
    .equ NVIC_ICER, 0xE000E180
    .equ NVIC_ISPR, 0xE000E200
    .equ NVIC_ICPR, 0xE000E280
    .equ NVIC_IPR,  0xE000E400

/* Prints "name" and the log entries, then clears the log. */
.macro PRINT_LOG name
    ldr   r7, =USART3_BASE
    PUTS  \name
    PUTC  '\n'
    bl    print_log
.endm

/* Prints "name" and the word at addr. */
.macro PRINT_WORD name, addr
    ldr   r7, =USART3_BASE
    PUTS  \name
    ldr   r0, =\addr
    ldr   r2, [r0]
    PRINT_HEX r2
    PUTC  '\n'
.endm

/* Prints "name" and register reg (r0, r2 or r5). */
.macro PRINT_REG name, reg
    mov   r5, \reg
    ldr   r7, =USART3_BASE
    PUTS  \name
    PRINT_HEX r5
    PUTC  '\n'
.endm

/* ACTIONS[n] = fn (0: none) */
.macro ACTION n, fn
    ldr   r0, =ACTIONS + (4 * \n)
    ldr   r1, =\fn
    str   r1, [r0]
.endm

.macro STORE addr, value
    ldr   r0, =\addr
    ldr   r1, =\value
    str   r1, [r0]
.endm

    .section .vectors, "a"
    .word _estack
    .word reset_handler

    .text
/* The vector table of the test: every exception to common_handler. */
    .align 9
vectors:
    .word _estack
    .word reset_handler
    .rept 46
    .word common_handler
    .endr

    .thumb_func
common_handler:
    ldr   r0, =LOG_COUNT
    ldr   r1, [r0]
    adds  r2, r1, #1
    str   r2, [r0]
    ldr   r0, =LOG
    add   r0, r0, r1, lsl #5
    mrs   r1, ipsr
    str   r1, [r0, #0]
    str   lr, [r0, #4]
    tst   lr, #4
    ite   eq
    mrseq r2, msp
    mrsne r2, psp
    str   r2, [r0, #8]
    ldr   r3, [r2, #0]
    str   r3, [r0, #12]
    ldr   r3, [r2, #24]
    str   r3, [r0, #16]
    ldr   r3, [r2, #28]
    str   r3, [r0, #20]
    mrs   r3, control
    str   r3, [r0, #24]
    ldr   r3, =SCS_CFSR
    ldr   r3, [r3]
    str   r3, [r0, #28]
    /* a fault: skip the faulting instruction, clear CFSR and HFSR */
    ldr   r3, =SKIP
    ldr   r3, [r3]
    cbz   r3, 1f
    ldr   r12, [r2, #24]
    add   r12, r12, r3
    str   r12, [r2, #24]
    ldr   r3, =SCS_CFSR
    mov   r12, #-1
    str   r12, [r3]
    str   r12, [r3, #4]
1:
    /* the scenario's action */
    ldr   r3, =ACTIONS
    ldr   r3, [r3, r1, lsl #2]
    cbz   r3, 2f
    push  {r4, lr}
    mov   r0, r2
    blx   r3
    pop   {r4, lr}
    cbz   r0, 2f
    mov   lr, r0
2:
    bx    lr
    .ltorg

/* Prints the log entries and clears the log and the actions. */
    .thumb_func
print_log:
    push  {r8, r9, lr}
    ldr   r8, =LOG
    ldr   r9, =LOG_COUNT
    ldr   r9, [r9]
plog_entry:
    cmp   r9, #0
    beq   plog_done
    ldr   r7, =USART3_BASE
    PUTC  ' '
    .irp k, 0, 4, 8, 12, 16, 20, 24, 28
    ldr   r2, [r8, #\k]
    PRINT_HEX r2
    .endr
    PUTC  '\n'
    add   r8, r8, #32
    sub   r9, r9, #1
    b     plog_entry
plog_done:
    ldr   r0, =LOG_COUNT
    movs  r1, #0
    str   r1, [r0]
    ldr   r0, =SKIP
    str   r1, [r0]
    ldr   r0, =ACTIONS
    movs  r2, #32
plog_clear:
    str   r1, [r0], #4
    subs  r2, #1
    bne   plog_clear
    pop   {r8, r9, pc}
    .ltorg

/* Actions (r0 = the frame) */

/* IRQ3: pends IRQ4 (higher priority: preempts now) and IRQ5 (lower: after) */
    .thumb_func
act_irq3_nest:
    ldr   r1, =NVIC_ISPR
    movs  r2, #0x30
    str   r2, [r1]
    dsb
    isb
    movs  r0, #0
    bx    lr

/* SVC: returns with an illegal EXC_RETURN (0xFFFFFFF5) */
    .thumb_func
act_svc_bad_return:
    ldr   r0, =0xFFFFFFF5
    bx    lr

/* UsageFault after the illegal return: back to the SVC caller's frame, Thread mode */
    .thumb_func
act_uf_fix_return:
    ldr   r0, =0xFFFFFFF9
    bx    lr

/* UsageFault INVSTATE: set the stacked T bit to go on at the stacked PC */
    .thumb_func
act_uf_set_t:
    ldr   r1, [r0, #28]
    orr   r1, r1, #0x01000000
    str   r1, [r0, #28]
    movs  r0, #0
    bx    lr

/* SVC with floating point: the handler's first FP instruction preserves the
 * caller's context; FPCCR before and after into VAR, VAR+4 */
    .thumb_func
act_svc_fp:
    ldr   r1, =SCS_FPCCR
    ldr   r2, [r1]
    ldr   r3, =VAR
    str   r2, [r3]
    vmov.f32 s0, #2.0
    vmov.f32 s1, #4.0
    ldr   r2, [r1]
    str   r2, [r3, #4]
    movs  r0, #0
    bx    lr

/* SVC that drops to unprivileged Thread mode on return (CONTROL.nPRIV) */
    .thumb_func
act_svc_unpriv:
    mrs   r1, control
    orr   r1, r1, #1
    msr   control, r1
    movs  r0, #0
    bx    lr

/* SVC that returns to privileged Thread mode */
    .thumb_func
act_svc_priv:
    mrs   r1, control
    bic   r1, r1, #1
    msr   control, r1
    movs  r0, #0
    bx    lr

/* IRQ3: an SVC at the IRQ's priority: escalated to HardFault */
    .thumb_func
act_irq3_svc:
    push  {lr}
    svc   #9
    pop   {lr}
    movs  r0, #0
    bx    lr

    .ltorg

    .thumb_func
    .global reset_handler
reset_handler:
    USART_INIT
    /* clear 0x20000000..0x20000FFF */
    ldr   r1, =0x20000000
    ldr   r2, =0x20001000
    movs  r0, #0
1:
    str   r0, [r1], #4
    cmp   r1, r2
    bne   1b
    STORE SCS_VTOR, vectors
    STORE SCS_CPACR, 0x00F00000
    dsb
    isb

    /* 1. SVC from Thread mode on the main stack, SP not 8-aligned */
    sub   sp, sp, #4
    movs  r0, #0x5A
    svc   #5
    add   sp, sp, #4
    PRINT_LOG svc_msp

    /* 2. PendSV by ICSR, then SysTick by ICSR (PENDSTSET) */
    STORE SCS_ICSR, 0x10000000
    dsb
    isb
    STORE SCS_ICSR, 0x04000000
    dsb
    isb
    PRINT_LOG pendsv_systick

    /* 3. Interrupt priorities: IRQ3 0x80, IRQ4 0x40, IRQ5 0xC0; enable 3,4,5 */
    ldr   r0, =NVIC_IPR + 3
    movs  r1, #0x80
    strb  r1, [r0]
    ldr   r0, =NVIC_IPR + 4
    movs  r1, #0x40
    strb  r1, [r0]
    movs  r1, #0xC0
    strb  r1, [r0, #1]
    ldr   r0, =NVIC_IPR
    ldr   r2, [r0]
    PRINT_REG ipr0, r2
    ldr   r0, =NVIC_IPR + 4
    ldr   r2, [r0]
    PRINT_REG ipr1, r2
    STORE NVIC_ISER, 0x38
    /* IRQ3, whose handler pends IRQ4 (nested) and IRQ5 (after IRQ3: tail-chained) */
    ACTION 19, act_irq3_nest
    STORE NVIC_ISPR, 0x08
    dsb
    isb
    PRINT_LOG nesting

    /* 4. PRIMASK: pending but masked; ICSR and ISPR; WFI does not wait; CPSIE takes it */
    cpsid i
    STORE NVIC_ISPR, 0x08
    dsb
    isb
    ldr   r0, =SCS_ICSR
    ldr   r2, [r0]
    PRINT_REG icsr_masked, r2
    ldr   r0, =NVIC_ISPR
    ldr   r2, [r0]
    PRINT_REG ispr_masked, r2
    wfi
    mrs   r2, primask
    PRINT_REG primask, r2
    cpsie i
    isb
    PRINT_LOG primask_released

    /* 5. BASEPRI: 0x80 masks IRQ3 (0x80), not IRQ4 (0x40); BASEPRI_MAX */
    movs  r0, #0x80
    msr   basepri, r0
    STORE NVIC_ISPR, 0x18
    dsb
    isb
    PRINT_LOG basepri_80
    movs  r0, #0xC0
    msr   basepri_max, r0
    mrs   r2, basepri
    PRINT_REG basepri_max_c0, r2
    movs  r0, #0x40
    msr   basepri_max, r0
    mrs   r2, basepri
    PRINT_REG basepri_max_40, r2
    movs  r0, #0
    msr   basepri, r0
    isb
    PRINT_LOG basepri_0

    /* 6. PRIGROUP 7: one group for all priorities: IRQ4 does not preempt IRQ3;
     *    after IRQ3, IRQ4 then IRQ5 by priority */
    STORE SCS_AIRCR, 0x05FA0700
    ACTION 19, act_irq3_nest
    STORE NVIC_ISPR, 0x08
    dsb
    isb
    STORE SCS_AIRCR, 0x05FA0000
    ldr   r0, =SCS_AIRCR
    ldr   r2, [r0]
    PRINT_REG aircr, r2
    PRINT_LOG prigroup_7

    /* 7. The process stack: SVC from Thread mode on PSP */
    ldr   r0, =PSP_TOP
    msr   psp, r0
    movs  r0, #2
    msr   control, r0
    isb
    movs  r0, #0x77
    svc   #7
    mrs   r2, psp
    PRINT_REG psp, r2
    mrs   r2, control
    PRINT_REG control_psp, r2
    movs  r0, #0
    msr   control, r0
    isb
    PRINT_LOG svc_psp

    /* 8. Unprivileged Thread mode: MSR to PRIMASK and CPSID ignored; SVC back */
    ACTION 11, act_svc_unpriv
    svc   #1
    mrs   r2, control
    PRINT_REG control_unpriv, r2
    movs  r0, #1
    msr   primask, r0
    cpsid i
    mrs   r2, primask
    PRINT_REG primask_unpriv, r2
    mrs   r2, msp
    PRINT_REG msp_unpriv, r2
    ACTION 11, act_svc_priv
    svc   #2
    mrs   r2, control
    PRINT_REG control_priv, r2
    PRINT_LOG unprivileged

    /* 9. Faults: UDF and SDIV by zero as UsageFaults, then UDF with UsageFault disabled: HardFault */
    STORE SCS_SHCSR, 0x00040000     /* USGFAULTENA */
    STORE SKIP, 2
    udf   #7
    ldr   r0, =SCS_CCR
    ldr   r1, [r0]
    orr   r1, r1, #0x10             /* DIV_0_TRP */
    str   r1, [r0]
    STORE SKIP, 4
    movs  r1, #9
    movs  r2, #0
    sdiv  r3, r1, r2
    ldr   r0, =SCS_CCR
    ldr   r1, [r0]
    bic   r1, r1, #0x10
    str   r1, [r0]
    udiv  r3, r1, r2
    PRINT_REG udiv_by_0, r3
    STORE SCS_SHCSR, 0
    STORE SKIP, 2
    udf   #8
    PRINT_LOG faults

    /* 10. INVSTATE: BX to an even address; the handler sets T and goes on there */
    STORE SCS_SHCSR, 0x00040000
    ACTION 6, act_uf_set_t
    ldr   r1, =invstate_target
    bic   r1, r1, #1
    bx    r1
    .align 2
    .thumb_func
invstate_target:
    nop
    PRINT_LOG invstate

    /* 11. An illegal EXC_RETURN: UsageFault INVPC; its handler returns to the caller */
    ACTION 11, act_svc_bad_return
    ACTION 6, act_uf_fix_return
    movs  r0, #0x11
    svc   #3
    ldr   r0, =SCS_SHCSR
    ldr   r2, [r0]
    PRINT_REG shcsr, r2
    PRINT_LOG invpc

    /* 12. SVC from an IRQ handler at a higher priority than SVCall: HardFault FORCED */
    STORE SCS_SHPR2, 0xC0000000     /* SVCall 0xC0 */
    ACTION 19, act_irq3_svc
    STORE NVIC_ISPR, 0x08
    dsb
    isb
    ldr   r0, =SCS_HFSR
    ldr   r2, [r0]
    PRINT_REG hfsr, r2
    STORE SCS_HFSR, 0xFFFFFFFF
    STORE SCS_SHPR2, 0
    PRINT_LOG svc_escalated

    /* 13. The floating-point context: lazy preservation by the handler's FP instruction */
    vmov.f32 s0, #1.5
    vmov.f32 s1, #3.0
    vmov.f32 s15, #-0.5
    ACTION 11, act_svc_fp
    svc   #4
    vmov  r2, s0
    PRINT_REG s0_after, r2
    vmov  r2, s1
    PRINT_REG s1_after, r2
    /* FPCCR without bit 10: QEMU sets it (SFRDY of Armv8-M's Security
     * Extension), a reserved bit of Armv7-M */
    ldr   r0, =VAR
    ldr   r2, [r0]
    bic   r2, r2, #0x400
    PRINT_REG fpccr_before_fp, r2
    ldr   r0, =VAR
    ldr   r2, [r0, #4]
    bic   r2, r2, #0x400
    PRINT_REG fpccr_after_fp, r2
    ldr   r0, =SCS_FPCAR
    ldr   r2, [r0]
    PRINT_REG fpcar, r2
    ldr   r0, =SCS_FPCAR
    ldr   r0, [r0]
    ldr   r2, [r0]
    PRINT_REG frame_s0, r2
    ldr   r0, =SCS_FPCAR
    ldr   r0, [r0]
    ldr   r2, [r0, #60]
    PRINT_REG frame_s15, r2
    mrs   r2, control
    PRINT_REG control_fp, r2
    PRINT_LOG fp_lazy

    /* 14. ... and a handler without FP: nothing written, LSPACT cleared on return */
    vmov.f32 s2, #5.0
    svc   #6
    ldr   r0, =SCS_FPCCR
    ldr   r2, [r0]
    bic   r2, r2, #0x400
    PRINT_REG fpccr_after, r2
    vmov  r2, s2
    PRINT_REG s2_after, r2
    PRINT_LOG fp_untouched

    /* 16. Equal priorities pending together: the lower exception number first
     *     (PendSV, SysTick at 0; IRQ4, IRQ5 at 0x40) */
    ldr   r0, =NVIC_IPR + 5
    movs  r1, #0x40
    strb  r1, [r0]
    cpsid i
    STORE NVIC_ISPR, 0x30
    STORE SCS_ICSR, 0x14000000
    cpsie i
    isb
    PRINT_LOG equal_priorities

    /* 17. The waiting hints that do not wait: SEV then WFE (the event is set),
     *     WFI.W with an interrupt pending under PRIMASK; UDF.W; BKPT */
    sev
    wfe
    sev.w
    wfe.w
    cpsid i
    STORE NVIC_ISPR, 0x08
    wfi.w
    cpsie i
    isb
    STORE SCS_SHCSR, 0x00040000
    STORE SKIP, 4
    udf.w #9
    STORE SKIP, 2
    bkpt  #1
    ldr   r0, =SCS_HFSR
    ldr   r2, [r0]
    PRINT_REG hfsr_bkpt, r2
    PRINT_LOG hints_udf_bkpt

    /* 15. The special registers */
    mrs   r2, msp
    PRINT_REG msp, r2
    movs  r0, #1
    msr   faultmask, r0
    mrs   r2, faultmask
    PRINT_REG faultmask, r2
    cpsie f
    mrs   r2, faultmask
    PRINT_REG faultmask_cleared, r2
    ldr   r0, =SCS_ICSR
    ldr   r2, [r0]
    PRINT_REG icsr_end, r2
    ldr   r0, =SCS_SHCSR
    ldr   r2, [r0]
    PRINT_REG shcsr_end, r2

    ldr   r7, =USART3_BASE
    PUTS  end
    PUTC  '\n'
halt:
    b     halt
    .ltorg

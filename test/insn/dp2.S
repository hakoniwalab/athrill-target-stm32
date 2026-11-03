/*
 * Instruction test for Athrill's STM32 target: the data-processing
 * instructions beyond dp.S (ORR, ORN, EOR, BIC, TST, TEQ, MVN, CMN, ADC, SBC,
 * RSB, SUB register), one case per Thumb encoding (S, non-S and RRX forms),
 * and BX, NOP and STRB (immediate, T1).
 *
 * Each case prints its result and, when it sets flags, NZCV; the expected
 * output is QEMU's (build.sh). SETC or CLRC before a case fixes C (and V) when
 * the result or the flags depend on it; "movs r5, #0" before a flag-setting
 * instruction clears the flag record (it sets only N and Z).
 */
    .syntax unified
    .cpu cortex-m7
    .thumb

#include "macros.inc"

    .section .vectors, "a"
    .word _estack
    .word reset_handler

    .text
    .thumb_func
    .global reset_handler
reset_handler:
    USART_INIT

    LDC   r1, 0x00000003
    LDC   r2, 0x00000005
    movs  r5, #0
    subs  r0, r1, r2
    SHOWF sub_reg_t1, r0

    LDC   r0, 0xF0F0F0F0
    LDC   r1, 0xF0F0F0F0
    SETC
    movs  r5, #0
    eors  r0, r1
    SHOWF eor_reg_t1, r0

    LDC   r0, 0xFFFFFFFF
    LDC   r1, 0x00000000
    SETC
    movs  r5, #0
    adcs  r0, r1
    SHOWF adc_reg_t1_c1, r0

    LDC   r0, 0x7FFFFFFF
    LDC   r1, 0x00000001
    CLRC
    movs  r5, #0
    adcs  r0, r1
    SHOWF adc_reg_t1_c0, r0

    LDC   r0, 0x00000005
    LDC   r1, 0x00000003
    CLRC
    movs  r5, #0
    sbcs  r0, r1
    SHOWF sbc_reg_t1_c0, r0

    LDC   r0, 0x00000003
    LDC   r1, 0x00000005
    SETC
    movs  r5, #0
    sbcs  r0, r1
    SHOWF sbc_reg_t1_c1, r0

    LDC   r1, 0x80000000
    LDC   r2, 0x80000001
    SETC
    movs  r5, #0
    tst   r1, r2
    SHOWC tst_reg_t1

    LDC   r1, 0x00000001
    SETC
    movs  r5, #0
    negs  r0, r1
    SHOWF rsb_imm_t1, r0

    LDC   r1, 0x00000000
    CLRC
    movs  r5, #0
    negs  r0, r1
    SHOWF rsb_imm_t1_0, r0

    LDC   r1, 0xFFFFFFFF
    LDC   r2, 0x00000001
    movs  r5, #0
    cmn   r1, r2
    SHOWC cmn_reg_t1

    LDC   r0, 0x80000000
    LDC   r1, 0x00000001
    CLRC
    movs  r5, #0
    orrs  r0, r1
    SHOWF orr_reg_t1, r0

    LDC   r0, 0x000000FF
    LDC   r1, 0x000000FF
    SETC
    movs  r5, #0
    bics  r0, r1
    SHOWF bic_reg_t1, r0

    LDC   r1, 0x00000000
    CLRC
    movs  r5, #0
    mvns  r0, r1
    SHOWF mvn_reg_t1, r0

    LDC   r1, 0x80000000
    CLRC
    movs  r5, #0
    tst.w r1, #0x80000000
    SHOWC tst_imm_t1

    LDC   r1, 0xFF00FF00
    SETC
    movs  r5, #0
    teq.w r1, #0xFF00FF00
    SHOWC teq_imm_t1

    LDC   r1, 0xFFFFFFFF
    movs  r5, #0
    cmn.w r1, #1
    SHOWC cmn_imm_t1

    LDC   r1, 0x12345678
    bic.w r0, r1, #0x000000FF
    SHOW  bic_imm_t1, r0

    LDC   r1, 0x80000000
    CLRC
    movs  r5, #0
    bics.w r0, r1, #0x80000000
    SHOWF bics_imm_t1, r0

    LDC   r1, 0x12000000
    orr.w r0, r1, #0x00FF00FF
    SHOW  orr_imm_t1, r0

    LDC   r1, 0x80000000
    SETC
    movs  r5, #0
    orrs.w r0, r1, #0x40000000
    SHOWF orrs_imm_t1, r0

    LDC   r1, 0x00000000
    orn.w r0, r1, #0x0000FF00
    SHOW  orn_imm_t1, r0

    LDC   r1, 0x00000000
    SETC
    movs  r5, #0
    orns.w r0, r1, #0x00FF00FF
    SHOWF orns_imm_t1, r0

    LDC   r1, 0x80000001
    eor.w r0, r1, #0x80000000
    SHOW  eor_imm_t1, r0

    LDC   r1, 0xFF000000
    CLRC
    movs  r5, #0
    eors.w r0, r1, #0xFF000000
    SHOWF eors_imm_t1, r0

    mvn.w r0, #0x00FF00FF
    SHOW  mvn_imm_t1, r0

    SETC
    movs  r5, #0
    mvns.w r0, #0x7F000000
    SHOWF mvns_imm_t1, r0

    LDC   r1, 0x00000001
    SETC
    adc.w r0, r1, #0x100
    SHOW  adc_imm_t1, r0

    LDC   r1, 0xFFFFFFFF
    CLRC
    movs  r5, #0
    adcs.w r0, r1, #1
    SHOWF adcs_imm_t1, r0

    LDC   r1, 0x00000005
    CLRC
    sbc.w r0, r1, #1
    SHOW  sbc_imm_t1, r0

    LDC   r1, 0x00FF00FF
    SETC
    movs  r5, #0
    sbcs.w r0, r1, #0x00FF00FF
    SHOWF sbcs_imm_t1, r0

    LDC   r1, 0x00000001
    rsb.w r0, r1, #0x100
    SHOW  rsb_imm_t2, r0

    LDC   r1, 0x80000000
    movs  r5, #0
    rsbs.w r0, r1, #0
    SHOWF rsbs_imm_t2, r0

    LDC   r1, 0x80000000
    LDC   r2, 0x40000000
    SETC
    movs  r5, #0
    tst.w r1, r2, lsl #1
    SHOWC tst_reg_t2

    LDC   r1, 0x80000000
    LDC   r2, 0x00000000
    SETC
    movs  r5, #0
    tst.w r1, r2, rrx
    SHOWC tst_reg_t2_rrx

    LDC   r1, 0xFFFFFFFF
    LDC   r2, 0x000000F0
    bic.w r0, r1, r2, lsr #4
    SHOW  bic_reg_t2, r0

    LDC   r1, 0xFFFFFFFE
    LDC   r2, 0x7FFFFFFF
    SETC
    movs  r5, #0
    bics.w r0, r1, r2, lsl #1
    SHOWF bics_reg_t2, r0

    LDC   r1, 0xFFFFFFFF
    LDC   r2, 0x00000000
    SETC
    bic.w r0, r1, r2, rrx
    SHOW  bic_reg_t2_rrx, r0

    LDC   r1, 0xFFFFFFFF
    LDC   r2, 0x00000001
    CLRC
    movs  r5, #0
    bics.w r0, r1, r2, rrx
    SHOWF bics_reg_t2_rrx, r0

    LDC   r1, 0x00001234
    LDC   r2, 0x00005678
    orr.w r0, r1, r2, lsl #16
    SHOW  orr_reg_t2, r0

    LDC   r1, 0x00000000
    LDC   r2, 0x80000000
    SETC
    movs  r5, #0
    orrs.w r0, r1, r2, asr #31
    SHOWF orrs_reg_t2, r0

    LDC   r1, 0x00000001
    LDC   r2, 0x00000000
    SETC
    orr.w r0, r1, r2, rrx
    SHOW  orr_reg_t2_rrx, r0

    LDC   r1, 0x00000000
    LDC   r2, 0x00000001
    CLRC
    movs  r5, #0
    orrs.w r0, r1, r2, rrx
    SHOWF orrs_reg_t2_rrx, r0

    LDC   r1, 0x00000000
    LDC   r2, 0x0000FF00
    orn.w r0, r1, r2, lsr #8
    SHOW  orn_reg_t1, r0

    LDC   r1, 0x00000000
    LDC   r2, 0x0000000F
    CLRC
    movs  r5, #0
    orns.w r0, r1, r2, ror #4
    SHOWF orns_reg_t1, r0

    LDC   r1, 0x00000000
    LDC   r2, 0xFFFFFFFF
    SETC
    orn.w r0, r1, r2, rrx
    SHOW  orn_reg_t1_rrx, r0

    LDC   r1, 0x00000000
    LDC   r2, 0x00000000
    CLRC
    movs  r5, #0
    orns.w r0, r1, r2, rrx
    SHOWF orns_reg_t1_rrx, r0

    LDC   r2, 0x0F000000
    mvn.w r0, r2, lsl #4
    SHOW  mvn_reg_t2, r0

    LDC   r2, 0xFFFFFFFF
    CLRC
    movs  r5, #0
    mvns.w r0, r2, lsr #1
    SHOWF mvns_reg_t2, r0

    LDC   r2, 0x00000000
    SETC
    mvn.w r0, r2, rrx
    SHOW  mvn_reg_t2_rrx, r0

    LDC   r2, 0xFFFFFFFF
    CLRC
    movs  r5, #0
    mvns.w r0, r2, rrx
    SHOWF mvns_reg_t2_rrx, r0

    LDC   r1, 0x000000FF
    LDC   r2, 0x000000FF
    eor.w r0, r1, r2, lsl #8
    SHOW  eor_reg_t2, r0

    LDC   r1, 0xF8000000
    LDC   r2, 0x80000000
    SETC
    movs  r5, #0
    eors.w r0, r1, r2, asr #4
    SHOWF eors_reg_t2, r0

    LDC   r1, 0x00000000
    LDC   r2, 0x00000002
    SETC
    eor.w r0, r1, r2, rrx
    SHOW  eor_reg_t2_rrx, r0

    LDC   r1, 0x80000000
    LDC   r2, 0x00000003
    CLRC
    movs  r5, #0
    eors.w r0, r1, r2, rrx
    SHOWF eors_reg_t2_rrx, r0

    LDC   r1, 0x00000002
    LDC   r2, 0x00000001
    SETC
    movs  r5, #0
    teq.w r1, r2, lsl #1
    SHOWC teq_reg_t1

    LDC   r1, 0x80000000
    LDC   r2, 0x00000000
    SETC
    movs  r5, #0
    teq.w r1, r2, rrx
    SHOWC teq_reg_t1_rrx

    LDC   r1, 0xFFFFFFFE
    LDC   r2, 0x00000001
    movs  r5, #0
    cmn.w r1, r2, lsl #1
    SHOWC cmn_reg_t2

    LDC   r1, 0x80000000
    LDC   r2, 0x00000000
    SETC
    movs  r5, #0
    cmn.w r1, r2, rrx
    SHOWC cmn_reg_t2_rrx

    LDC   r1, 0x00000001
    LDC   r2, 0x00000001
    SETC
    adc.w r0, r1, r2, lsl #4
    SHOW  adc_reg_t2, r0

    LDC   r1, 0x7FFFFFFF
    LDC   r2, 0x00000002
    CLRC
    movs  r5, #0
    adcs.w r0, r1, r2, lsr #1
    SHOWF adcs_reg_t2, r0

    LDC   r1, 0x00000001
    LDC   r2, 0x00000000
    SETC
    adc.w r0, r1, r2, rrx
    SHOW  adc_reg_t2_rrx, r0

    LDC   r1, 0xFFFFFFFF
    LDC   r2, 0x00000003
    CLRC
    movs  r5, #0
    adcs.w r0, r1, r2, rrx
    SHOWF adcs_reg_t2_rrx, r0

    LDC   r1, 0x00000064
    LDC   r2, 0x00000005
    SETC
    sbc.w r0, r1, r2, lsl #2
    SHOW  sbc_reg_t2, r0

    LDC   r1, 0x00000000
    LDC   r2, 0x00000000
    CLRC
    movs  r5, #0
    sbcs.w r0, r1, r2, asr #1
    SHOWF sbcs_reg_t2, r0

    LDC   r1, 0x80000000
    LDC   r2, 0x00000000
    SETC
    sbc.w r0, r1, r2, rrx
    SHOW  sbc_reg_t2_rrx, r0

    LDC   r1, 0x00000005
    LDC   r2, 0x00000004
    CLRC
    movs  r5, #0
    sbcs.w r0, r1, r2, rrx
    SHOWF sbcs_reg_t2_rrx, r0

    LDC   r1, 0x00000064
    LDC   r2, 0x0000000A
    sub.w r0, r1, r2, lsl #3
    SHOW  sub_reg_t2, r0

    LDC   r1, 0x00000000
    LDC   r2, 0x80000000
    movs  r5, #0
    subs.w r0, r1, r2, lsr #31
    SHOWF subs_reg_t2, r0

    LDC   r1, 0x80000001
    LDC   r2, 0x00000002
    SETC
    sub.w r0, r1, r2, rrx
    SHOW  sub_reg_t2_rrx, r0

    LDC   r1, 0x00000001
    LDC   r2, 0x00000002
    CLRC
    movs  r5, #0
    subs.w r0, r1, r2, rrx
    SHOWF subs_reg_t2_rrx, r0

    LDC   r1, 0x00000001
    LDC   r2, 0x00000001
    rsb.w r0, r1, r2, lsl #4
    SHOW  rsb_reg_t1, r0

    LDC   r1, 0x00000001
    LDC   r2, 0x00000000
    movs  r5, #0
    rsbs.w r0, r1, r2, asr #1
    SHOWF rsbs_reg_t1, r0

    LDC   r1, 0x00000001
    LDC   r2, 0x00000000
    SETC
    rsb.w r0, r1, r2, rrx
    SHOW  rsb_reg_t1_rrx, r0

    LDC   r1, 0x00000000
    LDC   r2, 0x00000000
    CLRC
    movs  r5, #0
    rsbs.w r0, r1, r2, rrx
    SHOWF rsbs_reg_t1_rrx, r0

    /* BX <Rm> (T1) to a Thumb address (bit 0 set by .thumb_func). */
    ldr   r0, =bx_target
    bx    r0
    PUTS  bx_t1_NG
    PUTC  '\n'
    .thumb_func
bx_target:
    PUTS  bx_t1_ok
    PUTC  '\n'
    POOL

    /* NOP (T1). */
    nop
    PUTS  nop_t1_ok
    PUTC  '\n'
    POOL

    /* STRB <Rt>, [<Rn>, #<imm5>] (T1), read back with LDRB. */
    LDC   r1, 0x20000100
    LDC   r0, 0x000001AB
    strb  r0, [r1, #1]
    movs  r0, #0
    ldrb  r0, [r1, #1]
    SHOW  strb_imm_t1, r0

    PUTS  end
    PUTC  '\n'
halt:
    wfi
    b     halt

    .ltorg

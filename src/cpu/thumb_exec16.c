/*
 * 16-bit Thumb instructions: decoded with the table generated from Arm's
 * machine-readable data (decoder/t32_decode16.h), executed here. An
 * instruction whose meaning is in athrill-arm-common has glue here that turns
 * its encoding into the pseudo-code's inputs (the encoding's "Encoding
 * specific operations" in the Armv7-M Architecture Reference Manual).
 */
#include "thumb_glue.h"
#include "t32_decode16.h"
#include "arm_common/arm_pseudo_load.h"
#include "arm_common/arm_pseudo_branch.h"

/*
 * Data processing, immediate
 */

/* ADD T1: ADDS <Rd>, <Rn>, #<imm3>
 *     d = UInt(Rd); n = UInt(Rn); setflags = !InITBlock(); imm32 = ZeroExtend(imm3, 32); */
static Std_ReturnType exec_add_imm_t1(TargetCoreType *core, uint16 insn)
{
	return thumb_dp_imm(core, arm_pseudo_add_imm, "ADD", !Stm32InITBlock(core),
		t32_ADD_i_T1_Rd(insn), t32_ADD_i_T1_Rn(insn), t32_ADD_i_T1_imm3(insn), FALSE);
}

/* ADD T2: ADDS <Rdn>, #<imm8>
 *     d = UInt(Rdn); n = UInt(Rdn); setflags = !InITBlock(); imm32 = ZeroExtend(imm8, 32); */
static Std_ReturnType exec_add_imm_t2(TargetCoreType *core, uint16 insn)
{
	return thumb_dp_imm(core, arm_pseudo_add_imm, "ADD", !Stm32InITBlock(core),
		t32_ADD_i_T2_Rdn(insn), t32_ADD_i_T2_Rdn(insn), t32_ADD_i_T2_imm8(insn), FALSE);
}

/* SUB T1: SUBS <Rd>, <Rn>, #<imm3>
 *     d = UInt(Rd); n = UInt(Rn); setflags = !InITBlock(); imm32 = ZeroExtend(imm3, 32); */
static Std_ReturnType exec_sub_imm_t1(TargetCoreType *core, uint16 insn)
{
	return thumb_dp_imm(core, arm_pseudo_sub_imm, "SUB", !Stm32InITBlock(core),
		t32_SUB_i_T1_Rd(insn), t32_SUB_i_T1_Rn(insn), t32_SUB_i_T1_imm3(insn), FALSE);
}

/* SUB T2: SUBS <Rdn>, #<imm8>
 *     d = UInt(Rdn); n = UInt(Rdn); setflags = !InITBlock(); imm32 = ZeroExtend(imm8, 32); */
static Std_ReturnType exec_sub_imm_t2(TargetCoreType *core, uint16 insn)
{
	return thumb_dp_imm(core, arm_pseudo_sub_imm, "SUB", !Stm32InITBlock(core),
		t32_SUB_i_T2_Rdn(insn), t32_SUB_i_T2_Rdn(insn), t32_SUB_i_T2_imm8(insn), FALSE);
}

/* CMP T1: CMP <Rn>, #<imm8>
 *     n = UInt(Rn); imm32 = ZeroExtend(imm8, 32); */
static Std_ReturnType exec_cmp_imm_t1(TargetCoreType *core, uint16 insn)
{
	return thumb_dp_imm(core, arm_pseudo_cmp_imm, "CMP", TRUE,
		THUMB_NO_REG, t32_CMP_i_T1_Rn(insn), t32_CMP_i_T1_imm8(insn), FALSE);
}

/* MOV T1: MOVS <Rd>, #<imm8>
 *     d = UInt(Rd); setflags = !InITBlock(); imm32 = ZeroExtend(imm8, 32); carry = APSR.C; */
static Std_ReturnType exec_mov_imm_t1(TargetCoreType *core, uint16 insn)
{
	bool carry = ApsrFlags(ArmAPSR(core)).carry;

	return thumb_dp_imm(core, arm_pseudo_mov_imm, "MOV", !Stm32InITBlock(core),
		t32_MOV_i_T1_Rd(insn), THUMB_NO_REG, t32_MOV_i_T1_imm8(insn), carry);
}

/*
 * Data processing, register
 */

/* ADD T1: ADDS <Rd>, <Rn>, <Rm>
 *     d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); setflags = !InITBlock();
 *     (shift_t, shift_n) = (SRType_LSL, 0); */
static Std_ReturnType exec_add_reg_t1(TargetCoreType *core, uint16 insn)
{
	return thumb_dp_reg(core, arm_pseudo_add_reg, "ADD", !Stm32InITBlock(core),
		t32_ADD_r_T1_Rd(insn), t32_ADD_r_T1_Rn(insn), t32_ADD_r_T1_Rm(insn), SRType_LSL, 0U);
}

/* ADD T2: ADD <Rdn>, <Rm>
 *     if (DN:Rdn) == '1101' || Rm == '1101' then SEE ADD (SP plus register);
 *     d = UInt(DN:Rdn); n = d; m = UInt(Rm); setflags = FALSE; (shift_t, shift_n) = (SRType_LSL, 0);
 *     if n == 15 && m == 15 then UNPREDICTABLE;
 *     if d == 15 && InITBlock() && !LastInITBlock() then UNPREDICTABLE; */
static Std_ReturnType exec_add_reg_t2(TargetCoreType *core, uint16 insn)
{
	uint32 d = (t32_ADD_r_T2_DN(insn) << 3) | t32_ADD_r_T2_Rdn(insn);
	uint32 m = t32_ADD_r_T2_Rm(insn);

	if ((d == 15U) && (m == 15U)) {
		return STD_E_INVALID;
	}
	return thumb_dp_reg(core, arm_pseudo_add_reg, "ADD", FALSE, d, d, m, SRType_LSL, 0U);
}

/* AND T1: ANDS <Rdn>, <Rm>
 *     d = UInt(Rdn); n = UInt(Rdn); m = UInt(Rm); setflags = !InITBlock();
 *     (shift_t, shift_n) = (SRType_LSL, 0); */
static Std_ReturnType exec_and_reg_t1(TargetCoreType *core, uint16 insn)
{
	return thumb_dp_reg(core, arm_pseudo_and_reg, "AND", !Stm32InITBlock(core),
		t32_AND_r_T1_Rdn(insn), t32_AND_r_T1_Rdn(insn), t32_AND_r_T1_Rm(insn), SRType_LSL, 0U);
}

/* CMP T1: CMP <Rn>, <Rm>
 *     n = UInt(Rn); m = UInt(Rm); (shift_t, shift_n) = (SRType_LSL, 0); */
static Std_ReturnType exec_cmp_reg_t1(TargetCoreType *core, uint16 insn)
{
	return thumb_dp_reg(core, arm_pseudo_cmp_reg, "CMP", TRUE,
		THUMB_NO_REG, t32_CMP_r_T1_Rn(insn), t32_CMP_r_T1_Rm(insn), SRType_LSL, 0U);
}

/* CMP T2: CMP <Rn>, <Rm> (high registers)
 *     n = UInt(N:Rn); m = UInt(Rm); (shift_t, shift_n) = (SRType_LSL, 0);
 *     if n < 8 && m < 8 then UNPREDICTABLE;
 *     if n == 15 || m == 15 then UNPREDICTABLE; */
static Std_ReturnType exec_cmp_reg_t2(TargetCoreType *core, uint16 insn)
{
	uint32 n = (t32_CMP_r_T2_N(insn) << 3) | t32_CMP_r_T2_Rn(insn);
	uint32 m = t32_CMP_r_T2_Rm(insn);

	if (((n < 8U) && (m < 8U)) || (n == 15U) || (m == 15U)) {
		return STD_E_INVALID;
	}
	return thumb_dp_reg(core, arm_pseudo_cmp_reg, "CMP", TRUE, THUMB_NO_REG, n, m, SRType_LSL, 0U);
}

/* MOV T1: MOV <Rd>, <Rm>
 *     d = UInt(D:Rd); m = UInt(Rm); setflags = FALSE;
 *     if d == 15 && InITBlock() && !LastInITBlock() then UNPREDICTABLE; */
static Std_ReturnType exec_mov_reg_t1(TargetCoreType *core, uint16 insn)
{
	uint32 d = (t32_MOV_r_T1_D(insn) << 3) | t32_MOV_r_T1_Rd(insn);

	return thumb_dp_reg(core, arm_pseudo_mov_reg, "MOV", FALSE,
		d, THUMB_NO_REG, t32_MOV_r_T1_Rm(insn), SRType_LSL, 0U);
}

/* MOV T2: MOVS <Rd>, <Rm> and LSLS/LSRS/ASRS <Rd>, <Rm>, #<imm5>
 *     d = UInt(Rd); m = UInt(Rm); setflags = !InITBlock();
 *     (shift_t, shift_n) = DecodeImmShift(op, imm5);
 *     MOVS <Rd>, <Rm> (op = '00', imm5 = '00000'): if InITBlock() then UNPREDICTABLE;
 * (op = '11' is another instruction in the generated decoder.) */
static Std_ReturnType exec_mov_reg_t2(TargetCoreType *core, uint16 insn)
{
	SRType shift_t;
	uint32 shift_n;

	DecodeImmShift(t32_MOV_r_T2_op(insn), t32_MOV_r_T2_imm5(insn), &shift_t, &shift_n);
	if ((shift_t == SRType_LSL) && (shift_n == 0U) && Stm32InITBlock(core)) {
		return STD_E_INVALID;
	}
	return thumb_dp_reg(core, arm_pseudo_mov_reg, "MOV", !Stm32InITBlock(core),
		t32_MOV_r_T2_Rd(insn), THUMB_NO_REG, t32_MOV_r_T2_Rm(insn), shift_t, shift_n);
}

/* MOV (register-shifted register) T1: LSLS/LSRS/ASRS/RORS <Rdm>, <Rs>
 *     d = UInt(Rdm); m = UInt(Rdm); s = UInt(Rs); setflags = !InITBlock();
 *     shift_t by the instruction (one generated id each). */
static Std_ReturnType mov_rsr_t1(TargetCoreType *core, uint32 rdm, uint32 rs, SRType shift_t)
{
	return thumb_mov_rsr(core, "MOV", !Stm32InITBlock(core), rdm, rdm, rs, shift_t);
}

static Std_ReturnType exec_lsl_reg_t1(TargetCoreType *core, uint16 insn)
{
	return mov_rsr_t1(core, t32_MOV_rr_T1_LSL_Rdm(insn), t32_MOV_rr_T1_LSL_Rs(insn), SRType_LSL);
}

static Std_ReturnType exec_lsr_reg_t1(TargetCoreType *core, uint16 insn)
{
	return mov_rsr_t1(core, t32_MOV_rr_T1_LSR_Rdm(insn), t32_MOV_rr_T1_LSR_Rs(insn), SRType_LSR);
}

static Std_ReturnType exec_asr_reg_t1(TargetCoreType *core, uint16 insn)
{
	return mov_rsr_t1(core, t32_MOV_rr_T1_ASR_Rdm(insn), t32_MOV_rr_T1_ASR_Rs(insn), SRType_ASR);
}

static Std_ReturnType exec_ror_reg_t1(TargetCoreType *core, uint16 insn)
{
	return mov_rsr_t1(core, t32_MOV_rr_T1_ROR_Rdm(insn), t32_MOV_rr_T1_ROR_Rs(insn), SRType_ROR);
}

/*
 * LDR (immediate)
 */

/* T1: LDR <Rt>, [<Rn>{, #<imm5:'00'>}]
 *     t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm5:'00', 32);
 *     index = TRUE; add = TRUE; wback = FALSE; */
static Std_ReturnType exec_ldr_imm_t1(TargetCoreType *core, uint16 insn)
{
	arm_ldr_imm_input_type in;
	arm_ldr_imm_output_type out;

	in.instrName = "LDR";
	in.cond = Stm32CurrentCond(core);
	ARM_SET_REG(core, &in, Rt, t32_LDR_i_T1_Rt(insn));
	ARM_SET_REG(core, &in, Rn, t32_LDR_i_T1_Rn(insn));
	in.imm32 = t32_LDR_i_T1_imm5(insn) << 2;
	in.index = TRUE;
	in.add = TRUE;
	in.wback = FALSE;
	return arm_pseudo_ldr_imm(core, &in, &out);
}

/* T2: LDR <Rt>, [SP{, #<imm8:'00'>}]
 *     t = UInt(Rt); n = 13; imm32 = ZeroExtend(imm8:'00', 32);
 *     index = TRUE; add = TRUE; wback = FALSE; */
static Std_ReturnType exec_ldr_imm_t2(TargetCoreType *core, uint16 insn)
{
	arm_ldr_imm_input_type in;
	arm_ldr_imm_output_type out;

	in.instrName = "LDR";
	in.cond = Stm32CurrentCond(core);
	ARM_SET_REG(core, &in, Rt, t32_LDR_i_T2_Rt(insn));
	ARM_SET_REG(core, &in, Rn, CPU_REG_SP);
	in.imm32 = t32_LDR_i_T2_imm8(insn) << 2;
	in.index = TRUE;
	in.add = TRUE;
	in.wback = FALSE;
	return arm_pseudo_ldr_imm(core, &in, &out);
}

/*
 * B
 */

/* T1: B<c> <label>
 *     if cond == '1110' then UNDEFINED; if cond == '1111' then SEE SVC;
 *     imm32 = SignExtend(imm8:'0', 32);
 *     if InITBlock() then UNPREDICTABLE;
 * (cond 1110 and 1111 are other instructions in the generated decoder.) */
static Std_ReturnType exec_b_t1(TargetCoreType *core, uint16 insn)
{
	arm_b_input_type in;
	arm_b_output_type out;

	if (Stm32InITBlock(core)) {
		return STD_E_INVALID;
	}
	in.instrName = "B";
	in.cond = t32_B_T1_cond(insn);
	in.imm32 = SignExtend(t32_B_T1_imm8(insn) << 1, 9U);
	return arm_pseudo_b(core, &in, &out);
}

/* T2: B <label>
 *     imm32 = SignExtend(imm11:'0', 32);
 *     if InITBlock() && !LastInITBlock() then UNPREDICTABLE; */
static Std_ReturnType exec_b_t2(TargetCoreType *core, uint16 insn)
{
	arm_b_input_type in;
	arm_b_output_type out;

	in.instrName = "B";
	in.cond = Stm32CurrentCond(core);
	in.imm32 = SignExtend(t32_B_T2_imm11(insn) << 1, 12U);
	return arm_pseudo_b(core, &in, &out);
}

/*
 * Instructions not yet in athrill-arm-common.
 */

static Std_ReturnType load(TargetCoreType *core, uint32 address, uint32 size, uint32 *value)
{
	return ArmMemU_R(core, address, size, value);
}

static Std_ReturnType store(TargetCoreType *core, uint32 address, uint32 size, uint32 value)
{
	return ArmMemU_W(core, address, size, value);
}

static Std_ReturnType exec_ldr_literal(TargetCoreType *core, uint16 insn)
{
	return load(core, Align(ArmPC(core), 4U) + (t32_LDR_l_T1_imm8(insn) << 2), 4U,
		&core->reg.r[t32_LDR_l_T1_Rt(insn)]);
}

static Std_ReturnType exec_adr(TargetCoreType *core, uint16 insn)
{
	core->reg.r[t32_ADR_T1_Rd(insn)] = Align(ArmPC(core), 4U) + (t32_ADR_T1_imm8(insn) << 2);
	return STD_E_OK;
}

static Std_ReturnType exec_str_imm(TargetCoreType *core, uint16 insn)
{
	return store(core, core->reg.r[t32_STR_i_T1_Rn(insn)] + (t32_STR_i_T1_imm5(insn) << 2), 4U,
		core->reg.r[t32_STR_i_T1_Rt(insn)]);
}

static Std_ReturnType exec_ldrb_imm(TargetCoreType *core, uint16 insn)
{
	return load(core, core->reg.r[t32_LDRB_i_T1_Rn(insn)] + t32_LDRB_i_T1_imm5(insn), 1U,
		&core->reg.r[t32_LDRB_i_T1_Rt(insn)]);
}

static Std_ReturnType exec_strb_imm(TargetCoreType *core, uint16 insn)
{
	return store(core, core->reg.r[t32_STRB_i_T1_Rn(insn)] + t32_STRB_i_T1_imm5(insn), 1U,
		core->reg.r[t32_STRB_i_T1_Rt(insn)]);
}

/* BX Rm: BXWritePC(R[m]). */
static Std_ReturnType exec_bx(TargetCoreType *core, uint16 insn)
{
	return ArmBXWritePC(core, core->reg.r[t32_BX_T1_Rm(insn)]);
}

static Std_ReturnType exec_nop(TargetCoreType *core, uint16 insn)
{
	(void)core;
	(void)insn;
	return STD_E_OK;
}

/* WFI: there are no interrupt sources yet, so the core stays halted. */
static Std_ReturnType exec_wfi(TargetCoreType *core, uint16 insn)
{
	(void)insn;
	core->is_halt = TRUE;
	return STD_E_OK;
}

typedef Std_ReturnType (*ThumbExec16)(TargetCoreType *core, uint16 insn);

static const ThumbExec16 executors[T32_16_ID_NUM] = {
	[T32_16_ADD_i_T1] = exec_add_imm_t1,
	[T32_16_ADD_i_T2] = exec_add_imm_t2,
	[T32_16_SUB_i_T1] = exec_sub_imm_t1,
	[T32_16_SUB_i_T2] = exec_sub_imm_t2,
	[T32_16_CMP_i_T1] = exec_cmp_imm_t1,
	[T32_16_MOV_i_T1] = exec_mov_imm_t1,
	[T32_16_ADD_r_T1] = exec_add_reg_t1,
	[T32_16_ADD_r_T2] = exec_add_reg_t2,
	[T32_16_AND_r_T1] = exec_and_reg_t1,
	[T32_16_CMP_r_T1] = exec_cmp_reg_t1,
	[T32_16_CMP_r_T2] = exec_cmp_reg_t2,
	[T32_16_MOV_r_T1] = exec_mov_reg_t1,
	[T32_16_MOV_r_T2] = exec_mov_reg_t2,
	[T32_16_MOV_rr_T1_LSL] = exec_lsl_reg_t1,
	[T32_16_MOV_rr_T1_LSR] = exec_lsr_reg_t1,
	[T32_16_MOV_rr_T1_ASR] = exec_asr_reg_t1,
	[T32_16_MOV_rr_T1_ROR] = exec_ror_reg_t1,
	[T32_16_LDR_i_T1] = exec_ldr_imm_t1,
	[T32_16_LDR_i_T2] = exec_ldr_imm_t2,
	[T32_16_B_T1] = exec_b_t1,
	[T32_16_B_T2] = exec_b_t2,
	[T32_16_LDR_l_T1] = exec_ldr_literal,
	[T32_16_ADR_T1] = exec_adr,
	[T32_16_STR_i_T1] = exec_str_imm,
	[T32_16_LDRB_i_T1] = exec_ldrb_imm,
	[T32_16_STRB_i_T1] = exec_strb_imm,
	[T32_16_BX_T1] = exec_bx,
	[T32_16_NOP_T1] = exec_nop,
	[T32_16_WFI_T1] = exec_wfi,
};

Std_ReturnType thumb_execute16(TargetCoreType *core, uint16 insn)
{
	T32Decode16Id id = t32_decode16(insn);

	if (executors[id] == NULL) {
		return STD_E_DECODE;
	}
	return executors[id](core, insn);
}

/*
 * 16-bit Thumb instructions: decoded with the table generated from Arm's
 * machine-readable data (decoder/t32_decode16.h), executed here. An
 * instruction whose meaning is in athrill-arm-common has glue here that turns
 * its encoding into the pseudo-code's inputs (the encoding's "Encoding
 * specific operations" in the Armv7-M Architecture Reference Manual).
 */
#include "thumb_exec.h"
#include "t32_decode16.h"
#include "arm_common/arm_pseudo_arith.h"
#include "arm_common/arm_pseudo_load.h"
#include "arm_common/arm_pseudo_branch.h"

/*
 * ADD (immediate)
 */

/* T1: ADDS <Rd>, <Rn>, #<imm3>
 *     d = UInt(Rd); n = UInt(Rn); setflags = !InITBlock(); imm32 = ZeroExtend(imm3, 32); */
static Std_ReturnType exec_add_imm_t1(TargetCoreType *core, uint16 insn)
{
	arm_add_imm_input_type in;
	arm_add_imm_output_type out;

	in.instrName = "ADD";
	in.cond = Stm32CurrentCond(core);
	in.setflags = !Stm32InITBlock(core);
	ARM_SET_REG(core, &in, Rd, t32_ADD_i_T1_Rd(insn));
	ARM_SET_REG(core, &in, Rn, t32_ADD_i_T1_Rn(insn));
	in.imm32 = t32_ADD_i_T1_imm3(insn);
	return arm_pseudo_add_imm(core, &in, &out);
}

/* T2: ADDS <Rdn>, #<imm8>
 *     d = UInt(Rdn); n = UInt(Rdn); setflags = !InITBlock(); imm32 = ZeroExtend(imm8, 32); */
static Std_ReturnType exec_add_imm_t2(TargetCoreType *core, uint16 insn)
{
	arm_add_imm_input_type in;
	arm_add_imm_output_type out;

	in.instrName = "ADD";
	in.cond = Stm32CurrentCond(core);
	in.setflags = !Stm32InITBlock(core);
	ARM_SET_REG(core, &in, Rd, t32_ADD_i_T2_Rdn(insn));
	ARM_SET_REG(core, &in, Rn, t32_ADD_i_T2_Rdn(insn));
	in.imm32 = t32_ADD_i_T2_imm8(insn);
	return arm_pseudo_add_imm(core, &in, &out);
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

static void set_nz(TargetCoreType *core, uint32 result)
{
	PseudoCodeStatusFlagType flags = ApsrFlags(ArmAPSR(core));

	ArmAPSRWrite(core, ApsrUpdateNZCV(ArmAPSR(core), result, &flags));
}

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

/* MOVS Rd, #imm8: C is unchanged. */
static Std_ReturnType exec_movs_imm(TargetCoreType *core, uint16 insn)
{
	uint32 value = t32_MOV_i_T1_imm8(insn);

	core->reg.r[t32_MOV_i_T1_Rd(insn)] = value;
	set_nz(core, value);
	return STD_E_OK;
}

static Std_ReturnType exec_cmp_imm(TargetCoreType *core, uint16 insn)
{
	PseudoCodeStatusFlagType flags;
	uint32 result = AddWithCarry(core->reg.r[t32_CMP_i_T1_Rn(insn)], ~t32_CMP_i_T1_imm8(insn), TRUE, &flags);

	ArmAPSRWrite(core, ApsrUpdateNZCV(ArmAPSR(core), result, &flags));
	return STD_E_OK;
}

static Std_ReturnType exec_subs_imm8(TargetCoreType *core, uint16 insn)
{
	uint32 rdn = t32_SUB_i_T2_Rdn(insn);
	PseudoCodeStatusFlagType flags;
	uint32 result = AddWithCarry(core->reg.r[rdn], ~t32_SUB_i_T2_imm8(insn), TRUE, &flags);

	core->reg.r[rdn] = result;
	ArmAPSRWrite(core, ApsrUpdateNZCV(ArmAPSR(core), result, &flags));
	return STD_E_OK;
}

/*
 * MOVS Rd, Rm, <shift> #imm (LSLS, LSRS, ASRS immediate; op 0b11 is not this
 * instruction): (result, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C).
 */
static Std_ReturnType exec_movs_shift_imm(TargetCoreType *core, uint16 insn)
{
	SRType shift_t;
	uint32 shift_n;
	PseudoCodeStatusFlagType flags = ApsrFlags(ArmAPSR(core));
	uint32 result;

	DecodeImmShift(t32_MOV_r_T2_op(insn), t32_MOV_r_T2_imm5(insn), &shift_t, &shift_n);
	result = Shift_C(core->reg.r[t32_MOV_r_T2_Rm(insn)], shift_t, shift_n, flags.carry, &flags.carry);
	core->reg.r[t32_MOV_r_T2_Rd(insn)] = result;
	ArmAPSRWrite(core, ApsrUpdateNZCV(ArmAPSR(core), result, &flags));
	return STD_E_OK;
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
	[T32_16_LDR_i_T1] = exec_ldr_imm_t1,
	[T32_16_LDR_i_T2] = exec_ldr_imm_t2,
	[T32_16_B_T1] = exec_b_t1,
	[T32_16_B_T2] = exec_b_t2,
	[T32_16_LDR_l_T1] = exec_ldr_literal,
	[T32_16_ADR_T1] = exec_adr,
	[T32_16_STR_i_T1] = exec_str_imm,
	[T32_16_LDRB_i_T1] = exec_ldrb_imm,
	[T32_16_STRB_i_T1] = exec_strb_imm,
	[T32_16_MOV_i_T1] = exec_movs_imm,
	[T32_16_CMP_i_T1] = exec_cmp_imm,
	[T32_16_SUB_i_T2] = exec_subs_imm8,
	[T32_16_MOV_r_T2] = exec_movs_shift_imm,
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

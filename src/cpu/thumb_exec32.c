/*
 * 32-bit Thumb instructions: decoded with the code generated from Arm's
 * machine-readable data (decoder/t32_decode32.h), executed through
 * athrill-arm-common's pseudo-code. The glue here is each encoding's
 * "Encoding specific operations" in the Armv7-M Architecture Reference
 * Manual.
 */
#include "thumb_exec.h"
#include "t32_decode32.h"
#include "arm_common/arm_pseudo_arith.h"
#include "arm_common/arm_pseudo_load.h"
#include "arm_common/arm_pseudo_branch.h"

/*
 * ADD (immediate)
 */

/* T3: ADD{S}<c>.W <Rd>, <Rn>, #<const>
 *     if Rd == '1111' && S == '1' then SEE CMN (immediate);
 *     if Rn == '1101' then SEE ADD (SP plus immediate);
 *     d = UInt(Rd); n = UInt(Rn); setflags = (S == '1'); imm32 = ThumbExpandImm(i:imm3:imm8);
 *     if d == 13 || (d == 15 && S == '0') || n == 15 then UNPREDICTABLE;
 * (CMN and ADD (SP plus immediate) are other ids in the generated decoder.) */
static Std_ReturnType add_imm_t3(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 i, uint32 imm3, uint32 imm8)
{
	arm_add_imm_input_type in;
	arm_add_imm_output_type out;
	uint32 imm12 = (i << 11) | (imm3 << 8) | imm8;

	if ((d == 13U) || (d == 15U) || (n == 15U)) {
		return STD_E_INVALID;
	}
	in.instrName = "ADD";
	in.cond = Stm32CurrentCond(core);
	in.setflags = setflags;
	ARM_SET_REG(core, &in, Rd, d);
	ARM_SET_REG(core, &in, Rn, n);
	in.imm32 = ThumbExpandImm(imm12);
	return arm_pseudo_add_imm(core, &in, &out);
}

static Std_ReturnType exec_add_imm_t3(TargetCoreType *core, uint32 insn)
{
	return add_imm_t3(core, FALSE, t32w_ADD_i_T3_Rd(insn), t32w_ADD_i_T3_Rn(insn),
		t32w_ADD_i_T3_i(insn), t32w_ADD_i_T3_imm3(insn), t32w_ADD_i_T3_imm8(insn));
}

static Std_ReturnType exec_adds_imm_t3(TargetCoreType *core, uint32 insn)
{
	return add_imm_t3(core, TRUE, t32w_ADDS_i_T3_Rd(insn), t32w_ADDS_i_T3_Rn(insn),
		t32w_ADDS_i_T3_i(insn), t32w_ADDS_i_T3_imm3(insn), t32w_ADDS_i_T3_imm8(insn));
}

/* T4: ADDW<c> <Rd>, <Rn>, #<imm12>
 *     if Rn == '1111' then SEE ADR;
 *     if Rn == '1101' then SEE ADD (SP plus immediate);
 *     d = UInt(Rd); n = UInt(Rn); setflags = FALSE; imm32 = ZeroExtend(i:imm3:imm8, 32);
 *     if d IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType exec_add_imm_t4(TargetCoreType *core, uint32 insn)
{
	arm_add_imm_input_type in;
	arm_add_imm_output_type out;
	uint32 d = t32w_ADD_i_T4_Rd(insn);

	if ((d == 13U) || (d == 15U)) {
		return STD_E_INVALID;
	}
	in.instrName = "ADDW";
	in.cond = Stm32CurrentCond(core);
	in.setflags = FALSE;
	ARM_SET_REG(core, &in, Rd, d);
	ARM_SET_REG(core, &in, Rn, t32w_ADD_i_T4_Rn(insn));
	in.imm32 = (t32w_ADD_i_T4_i(insn) << 11) | (t32w_ADD_i_T4_imm3(insn) << 8) | t32w_ADD_i_T4_imm8(insn);
	return arm_pseudo_add_imm(core, &in, &out);
}

/*
 * LDR (immediate)
 */

/* Common checks of T3 and T4:
 *     if t == 15 && InITBlock() && !LastInITBlock() then UNPREDICTABLE;
 * InITBlock() is FALSE while IT is not executed. */
static Std_ReturnType ldr_imm(TargetCoreType *core, uint32 t, uint32 n, uint32 imm32, bool index, bool add, bool wback)
{
	arm_ldr_imm_input_type in;
	arm_ldr_imm_output_type out;

	in.instrName = "LDR";
	in.cond = Stm32CurrentCond(core);
	ARM_SET_REG(core, &in, Rt, t);
	ARM_SET_REG(core, &in, Rn, n);
	in.imm32 = imm32;
	in.index = index;
	in.add = add;
	in.wback = wback;
	return arm_pseudo_ldr_imm(core, &in, &out);
}

/* T3: LDR<c>.W <Rt>, [<Rn>{, #<imm12>}]
 *     if Rn == '1111' then SEE LDR (literal);
 *     t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm12, 32);
 *     index = TRUE; add = TRUE; wback = FALSE; */
static Std_ReturnType exec_ldr_imm_t3(TargetCoreType *core, uint32 insn)
{
	return ldr_imm(core, t32w_LDR_i_T3_Rt(insn), t32w_LDR_i_T3_Rn(insn), t32w_LDR_i_T3_imm12(insn),
		TRUE, TRUE, FALSE);
}

/* T4: LDR<c> <Rt>, [<Rn>, #-<imm8>] / [<Rn>], #+/-<imm8> / [<Rn>, #+/-<imm8>]!
 *     if Rn == '1111' then SEE LDR (literal);
 *     if P == '1' && U == '1' && W == '0' then SEE LDRT;
 *     if Rn == '1101' && P == '0' && U == '1' && W == '1' && imm8 == '00000100' then SEE POP;
 *     if P == '0' && W == '0' then UNDEFINED;
 *     t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm8, 32);
 *     index = (P == '1'); add = (U == '1'); wback = (W == '1');
 *     if wback && n == t then UNPREDICTABLE;
 * The generated decoder gives one id per P:W form (offset, post-indexed,
 * pre-indexed); LDRT, POP and the undefined form are other ids. */
static Std_ReturnType exec_ldr_imm_t4_off(TargetCoreType *core, uint32 insn)
{
	return ldr_imm(core, t32w_LDR_i_T4_off_Rt(insn), t32w_LDR_i_T4_off_Rn(insn), t32w_LDR_i_T4_off_imm8(insn),
		TRUE, FALSE, FALSE);
}

static Std_ReturnType exec_ldr_imm_t4_post(TargetCoreType *core, uint32 insn)
{
	uint32 t = t32w_LDR_i_T4_post_Rt(insn);
	uint32 n = t32w_LDR_i_T4_post_Rn(insn);

	if (n == t) {
		return STD_E_INVALID;
	}
	return ldr_imm(core, t, n, t32w_LDR_i_T4_post_imm8(insn), FALSE, t32w_LDR_i_T4_post_U(insn) != 0U, TRUE);
}

static Std_ReturnType exec_ldr_imm_t4_pre(TargetCoreType *core, uint32 insn)
{
	uint32 t = t32w_LDR_i_T4_pre_Rt(insn);
	uint32 n = t32w_LDR_i_T4_pre_Rn(insn);

	if (n == t) {
		return STD_E_INVALID;
	}
	return ldr_imm(core, t, n, t32w_LDR_i_T4_pre_imm8(insn), TRUE, t32w_LDR_i_T4_pre_U(insn) != 0U, TRUE);
}

/*
 * B
 */

/* T3: B<c>.W <label>
 *     if cond<3:1> == '111' then SEE "Related encodings";
 *     imm32 = SignExtend(S:J2:J1:imm6:imm11:'0', 32);
 *     if InITBlock() then UNPREDICTABLE; */
static Std_ReturnType exec_b_t3(TargetCoreType *core, uint32 insn)
{
	arm_b_input_type in;
	arm_b_output_type out;
	uint32 imm21 = (t32w_B_T3_S(insn) << 20)
		| (t32w_B_T3_J2(insn) << 19)
		| (t32w_B_T3_J1(insn) << 18)
		| (t32w_B_T3_imm6(insn) << 12)
		| (t32w_B_T3_imm11(insn) << 1);

	if (Stm32InITBlock(core)) {
		return STD_E_INVALID;
	}
	in.instrName = "B";
	in.cond = t32w_B_T3_cond(insn);
	in.imm32 = SignExtend(imm21, 21U);
	return arm_pseudo_b(core, &in, &out);
}

/* T4: B<c>.W <label>
 *     I1 = NOT(J1 EOR S); I2 = NOT(J2 EOR S);
 *     imm32 = SignExtend(S:I1:I2:imm10:imm11:'0', 32);
 *     if InITBlock() && !LastInITBlock() then UNPREDICTABLE; */
static Std_ReturnType exec_b_t4(TargetCoreType *core, uint32 insn)
{
	arm_b_input_type in;
	arm_b_output_type out;
	uint32 s = t32w_B_T4_S(insn);
	uint32 i1 = (t32w_B_T4_J1(insn) ^ s) ^ 1U;
	uint32 i2 = (t32w_B_T4_J2(insn) ^ s) ^ 1U;
	uint32 imm25 = (s << 24)
		| (i1 << 23)
		| (i2 << 22)
		| (t32w_B_T4_imm10(insn) << 12)
		| (t32w_B_T4_imm11(insn) << 1);

	in.instrName = "B";
	in.cond = Stm32CurrentCond(core);
	in.imm32 = SignExtend(imm25, 25U);
	return arm_pseudo_b(core, &in, &out);
}

typedef Std_ReturnType (*ThumbExec32)(TargetCoreType *core, uint32 insn);

static const ThumbExec32 executors[T32_32_ID_NUM] = {
	[T32_32_ADD_i_T3] = exec_add_imm_t3,
	[T32_32_ADDS_i_T3] = exec_adds_imm_t3,
	[T32_32_ADD_i_T4] = exec_add_imm_t4,
	[T32_32_LDR_i_T3] = exec_ldr_imm_t3,
	[T32_32_LDR_i_T4_off] = exec_ldr_imm_t4_off,
	[T32_32_LDR_i_T4_post] = exec_ldr_imm_t4_post,
	[T32_32_LDR_i_T4_pre] = exec_ldr_imm_t4_pre,
	[T32_32_B_T3] = exec_b_t3,
	[T32_32_B_T4] = exec_b_t4,
};

Std_ReturnType thumb_execute32(TargetCoreType *core, uint32 insn)
{
	T32Decode32Id id = t32_decode32(insn);

	if (executors[id] == NULL) {
		return STD_E_DECODE;
	}
	return executors[id](core, insn);
}

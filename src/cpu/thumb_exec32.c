/*
 * 32-bit Thumb instructions: decoded with the code generated from Arm's
 * machine-readable data (decoder/t32_decode32.h), executed through
 * athrill-arm-common's pseudo-code. The glue here is each encoding's
 * "Encoding specific operations" in the Armv7-M Architecture Reference
 * Manual.
 */
#include "thumb_glue.h"
#include "t32_decode32.h"
#include "arm_common/arm_pseudo_branch.h"

/*
 * Data processing, immediate
 */

/* ADD T3: ADD{S}<c>.W <Rd>, <Rn>, #<const>
 *     if Rd == '1111' && S == '1' then SEE CMN (immediate);
 *     if Rn == '1101' then SEE ADD (SP plus immediate);
 *     d = UInt(Rd); n = UInt(Rn); setflags = (S == '1'); imm32 = ThumbExpandImm(i:imm3:imm8);
 *     if d == 13 || (d == 15 && S == '0') || n == 15 then UNPREDICTABLE;
 * (CMN and ADD (SP plus immediate) are other ids in the generated decoder.) */
static Std_ReturnType add_imm_t3(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 imm12)
{
	if ((d == 13U) || (d == 15U) || (n == 15U)) {
		return STD_E_INVALID;
	}
	return thumb_dp_imm(core, arm_pseudo_add_imm, "ADD", setflags, d, n, ThumbExpandImm(imm12), FALSE);
}

static Std_ReturnType exec_add_imm_t3(TargetCoreType *core, uint32 insn)
{
	return add_imm_t3(core, FALSE, t32w_ADD_i_T3_Rd(insn), t32w_ADD_i_T3_Rn(insn),
		thumb_imm12(t32w_ADD_i_T3_i(insn), t32w_ADD_i_T3_imm3(insn), t32w_ADD_i_T3_imm8(insn)));
}

static Std_ReturnType exec_adds_imm_t3(TargetCoreType *core, uint32 insn)
{
	return add_imm_t3(core, TRUE, t32w_ADDS_i_T3_Rd(insn), t32w_ADDS_i_T3_Rn(insn),
		thumb_imm12(t32w_ADDS_i_T3_i(insn), t32w_ADDS_i_T3_imm3(insn), t32w_ADDS_i_T3_imm8(insn)));
}

/* ADD T4: ADDW<c> <Rd>, <Rn>, #<imm12>
 *     if Rn == '1111' then SEE ADR;
 *     if Rn == '1101' then SEE ADD (SP plus immediate);
 *     d = UInt(Rd); n = UInt(Rn); setflags = FALSE; imm32 = ZeroExtend(i:imm3:imm8, 32);
 *     if d IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType exec_add_imm_t4(TargetCoreType *core, uint32 insn)
{
	uint32 d = t32w_ADD_i_T4_Rd(insn);

	if (thumb_sp_or_pc(d)) {
		return STD_E_INVALID;
	}
	return thumb_dp_imm(core, arm_pseudo_add_imm, "ADDW", FALSE, d, t32w_ADD_i_T4_Rn(insn),
		thumb_imm12(t32w_ADD_i_T4_i(insn), t32w_ADD_i_T4_imm3(insn), t32w_ADD_i_T4_imm8(insn)), FALSE);
}

/* SUB T3: SUB{S}<c>.W <Rd>, <Rn>, #<const>
 *     if Rd == '1111' && S == '1' then SEE CMP (immediate);
 *     if Rn == '1101' then SEE SUB (SP minus immediate);
 *     d = UInt(Rd); n = UInt(Rn); setflags = (S == '1'); imm32 = ThumbExpandImm(i:imm3:imm8);
 *     if d == 13 || (d == 15 && S == '0') || n == 15 then UNPREDICTABLE; */
static Std_ReturnType sub_imm_t3(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 imm12)
{
	if ((d == 13U) || (d == 15U) || (n == 15U)) {
		return STD_E_INVALID;
	}
	return thumb_dp_imm(core, arm_pseudo_sub_imm, "SUB", setflags, d, n, ThumbExpandImm(imm12), FALSE);
}

static Std_ReturnType exec_sub_imm_t3(TargetCoreType *core, uint32 insn)
{
	return sub_imm_t3(core, FALSE, t32w_SUB_i_T3_Rd(insn), t32w_SUB_i_T3_Rn(insn),
		thumb_imm12(t32w_SUB_i_T3_i(insn), t32w_SUB_i_T3_imm3(insn), t32w_SUB_i_T3_imm8(insn)));
}

static Std_ReturnType exec_subs_imm_t3(TargetCoreType *core, uint32 insn)
{
	return sub_imm_t3(core, TRUE, t32w_SUBS_i_T3_Rd(insn), t32w_SUBS_i_T3_Rn(insn),
		thumb_imm12(t32w_SUBS_i_T3_i(insn), t32w_SUBS_i_T3_imm3(insn), t32w_SUBS_i_T3_imm8(insn)));
}

/* SUB T4: SUBW<c> <Rd>, <Rn>, #<imm12>
 *     if Rn == '1111' then SEE ADR;
 *     if Rn == '1101' then SEE SUB (SP minus immediate);
 *     d = UInt(Rd); n = UInt(Rn); setflags = FALSE; imm32 = ZeroExtend(i:imm3:imm8, 32);
 *     if d IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType exec_sub_imm_t4(TargetCoreType *core, uint32 insn)
{
	uint32 d = t32w_SUB_i_T4_Rd(insn);

	if (thumb_sp_or_pc(d)) {
		return STD_E_INVALID;
	}
	return thumb_dp_imm(core, arm_pseudo_sub_imm, "SUBW", FALSE, d, t32w_SUB_i_T4_Rn(insn),
		thumb_imm12(t32w_SUB_i_T4_i(insn), t32w_SUB_i_T4_imm3(insn), t32w_SUB_i_T4_imm8(insn)), FALSE);
}

/* CMP T2: CMP<c>.W <Rn>, #<const>
 *     n = UInt(Rn); imm32 = ThumbExpandImm(i:imm3:imm8);
 *     if n == 15 then UNPREDICTABLE; */
static Std_ReturnType exec_cmp_imm_t2(TargetCoreType *core, uint32 insn)
{
	uint32 n = t32w_CMP_i_T2_Rn(insn);

	if (n == 15U) {
		return STD_E_INVALID;
	}
	return thumb_dp_imm(core, arm_pseudo_cmp_imm, "CMP", TRUE, THUMB_NO_REG, n,
		ThumbExpandImm(thumb_imm12(t32w_CMP_i_T2_i(insn), t32w_CMP_i_T2_imm3(insn), t32w_CMP_i_T2_imm8(insn))), FALSE);
}

/* AND T1: AND{S}<c> <Rd>, <Rn>, #<const>
 *     if Rd == '1111' && S == '1' then SEE TST (immediate);
 *     d = UInt(Rd); n = UInt(Rn); setflags = (S == '1');
 *     (imm32, carry) = ThumbExpandImm_C(i:imm3:imm8, APSR.C);
 *     if d == 13 || (d == 15 && S == '0') || n IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType and_imm_t1(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 imm12)
{
	bool carry;
	uint32 imm32;

	if ((d == 13U) || (d == 15U) || thumb_sp_or_pc(n)) {
		return STD_E_INVALID;
	}
	imm32 = ThumbExpandImm_C(imm12, ApsrFlags(ArmAPSR(core)).carry, &carry);
	return thumb_dp_imm(core, arm_pseudo_and_imm, "AND", setflags, d, n, imm32, carry);
}

static Std_ReturnType exec_and_imm_t1(TargetCoreType *core, uint32 insn)
{
	return and_imm_t1(core, FALSE, t32w_AND_i_T1_Rd(insn), t32w_AND_i_T1_Rn(insn),
		thumb_imm12(t32w_AND_i_T1_i(insn), t32w_AND_i_T1_imm3(insn), t32w_AND_i_T1_imm8(insn)));
}

static Std_ReturnType exec_ands_imm_t1(TargetCoreType *core, uint32 insn)
{
	return and_imm_t1(core, TRUE, t32w_ANDS_i_T1_Rd(insn), t32w_ANDS_i_T1_Rn(insn),
		thumb_imm12(t32w_ANDS_i_T1_i(insn), t32w_ANDS_i_T1_imm3(insn), t32w_ANDS_i_T1_imm8(insn)));
}

/* MOV T2: MOV{S}<c>.W <Rd>, #<const>
 *     d = UInt(Rd); setflags = (S == '1');
 *     (imm32, carry) = ThumbExpandImm_C(i:imm3:imm8, APSR.C);
 *     if d IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType mov_imm_t2(TargetCoreType *core, bool setflags, uint32 d, uint32 imm12)
{
	bool carry;
	uint32 imm32;

	if (thumb_sp_or_pc(d)) {
		return STD_E_INVALID;
	}
	imm32 = ThumbExpandImm_C(imm12, ApsrFlags(ArmAPSR(core)).carry, &carry);
	return thumb_dp_imm(core, arm_pseudo_mov_imm, "MOV", setflags, d, THUMB_NO_REG, imm32, carry);
}

static Std_ReturnType exec_mov_imm_t2(TargetCoreType *core, uint32 insn)
{
	return mov_imm_t2(core, FALSE, t32w_MOV_i_T2_Rd(insn),
		thumb_imm12(t32w_MOV_i_T2_i(insn), t32w_MOV_i_T2_imm3(insn), t32w_MOV_i_T2_imm8(insn)));
}

static Std_ReturnType exec_movs_imm_t2(TargetCoreType *core, uint32 insn)
{
	return mov_imm_t2(core, TRUE, t32w_MOVS_i_T2_Rd(insn),
		thumb_imm12(t32w_MOVS_i_T2_i(insn), t32w_MOVS_i_T2_imm3(insn), t32w_MOVS_i_T2_imm8(insn)));
}

/* MOV T3: MOVW<c> <Rd>, #<imm16>
 *     d = UInt(Rd); setflags = FALSE; imm32 = ZeroExtend(imm4:i:imm3:imm8, 32);
 *     if d IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType exec_mov_imm_t3(TargetCoreType *core, uint32 insn)
{
	uint32 d = t32w_MOV_i_T3_Rd(insn);
	uint32 imm16 = (t32w_MOV_i_T3_imm4(insn) << 12)
		| thumb_imm12(t32w_MOV_i_T3_i(insn), t32w_MOV_i_T3_imm3(insn), t32w_MOV_i_T3_imm8(insn));

	if (thumb_sp_or_pc(d)) {
		return STD_E_INVALID;
	}
	return thumb_dp_imm(core, arm_pseudo_mov_imm, "MOVW", FALSE, d, THUMB_NO_REG, imm16, FALSE);
}

/*
 * Data processing, register
 *
 * The generated decoder gives the RRX form of each 32-bit register encoding
 * its own id (stype = '11', imm3:imm2 = '00000'); DecodeImmShift gives the
 * same (SRType_RRX, 1) for it.
 */

/* ADD T3: ADD{S}<c>.W <Rd>, <Rn>, <Rm>{, <shift>}
 *     if Rd == '1111' && S == '1' then SEE CMN (register);
 *     if Rn == '1101' then SEE ADD (SP plus register);
 *     d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); setflags = (S == '1');
 *     (shift_t, shift_n) = DecodeImmShift(type, imm3:imm2);
 *     if d == 13 || (d == 15 && S == '0') || n == 15 || m IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType add_reg_t3(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 m, uint32 type, uint32 imm5)
{
	SRType shift_t;
	uint32 shift_n;

	if ((d == 13U) || (d == 15U) || (n == 15U) || thumb_sp_or_pc(m)) {
		return STD_E_INVALID;
	}
	DecodeImmShift(type, imm5, &shift_t, &shift_n);
	return thumb_dp_reg(core, arm_pseudo_add_reg, "ADD", setflags, d, n, m, shift_t, shift_n);
}

static Std_ReturnType exec_add_reg_t3(TargetCoreType *core, uint32 insn)
{
	return add_reg_t3(core, FALSE, t32w_ADD_r_T3_Rd(insn), t32w_ADD_r_T3_Rn(insn), t32w_ADD_r_T3_Rm(insn),
		t32w_ADD_r_T3_stype(insn), thumb_imm5(t32w_ADD_r_T3_imm3(insn), t32w_ADD_r_T3_imm2(insn)));
}

static Std_ReturnType exec_adds_reg_t3(TargetCoreType *core, uint32 insn)
{
	return add_reg_t3(core, TRUE, t32w_ADDS_r_T3_Rd(insn), t32w_ADDS_r_T3_Rn(insn), t32w_ADDS_r_T3_Rm(insn),
		t32w_ADDS_r_T3_stype(insn), thumb_imm5(t32w_ADDS_r_T3_imm3(insn), t32w_ADDS_r_T3_imm2(insn)));
}

static Std_ReturnType exec_add_reg_t3_rrx(TargetCoreType *core, uint32 insn)
{
	return add_reg_t3(core, FALSE, t32w_ADD_r_T3_RRX_Rd(insn), t32w_ADD_r_T3_RRX_Rn(insn), t32w_ADD_r_T3_RRX_Rm(insn), 3U, 0U);
}

static Std_ReturnType exec_adds_reg_t3_rrx(TargetCoreType *core, uint32 insn)
{
	return add_reg_t3(core, TRUE, t32w_ADDS_r_T3_RRX_Rd(insn), t32w_ADDS_r_T3_RRX_Rn(insn), t32w_ADDS_r_T3_RRX_Rm(insn), 3U, 0U);
}

/* CMP T3: CMP<c>.W <Rn>, <Rm>{, <shift>}
 *     n = UInt(Rn); m = UInt(Rm); (shift_t, shift_n) = DecodeImmShift(type, imm3:imm2);
 *     if n == 15 || m IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType cmp_reg_t3(TargetCoreType *core, uint32 n, uint32 m, uint32 type, uint32 imm5)
{
	SRType shift_t;
	uint32 shift_n;

	if ((n == 15U) || thumb_sp_or_pc(m)) {
		return STD_E_INVALID;
	}
	DecodeImmShift(type, imm5, &shift_t, &shift_n);
	return thumb_dp_reg(core, arm_pseudo_cmp_reg, "CMP", TRUE, THUMB_NO_REG, n, m, shift_t, shift_n);
}

static Std_ReturnType exec_cmp_reg_t3(TargetCoreType *core, uint32 insn)
{
	return cmp_reg_t3(core, t32w_CMP_r_T3_Rn(insn), t32w_CMP_r_T3_Rm(insn),
		t32w_CMP_r_T3_stype(insn), thumb_imm5(t32w_CMP_r_T3_imm3(insn), t32w_CMP_r_T3_imm2(insn)));
}

static Std_ReturnType exec_cmp_reg_t3_rrx(TargetCoreType *core, uint32 insn)
{
	return cmp_reg_t3(core, t32w_CMP_r_T3_RRX_Rn(insn), t32w_CMP_r_T3_RRX_Rm(insn), 3U, 0U);
}

/* AND T2: AND{S}<c>.W <Rd>, <Rn>, <Rm>{, <shift>}
 *     if Rd == '1111' && S == '1' then SEE TST (register);
 *     d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); setflags = (S == '1');
 *     (shift_t, shift_n) = DecodeImmShift(type, imm3:imm2);
 *     if d == 13 || (d == 15 && S == '0') || n IN {13,15} || m IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType and_reg_t2(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 m, uint32 type, uint32 imm5)
{
	SRType shift_t;
	uint32 shift_n;

	if ((d == 13U) || (d == 15U) || thumb_sp_or_pc(n) || thumb_sp_or_pc(m)) {
		return STD_E_INVALID;
	}
	DecodeImmShift(type, imm5, &shift_t, &shift_n);
	return thumb_dp_reg(core, arm_pseudo_and_reg, "AND", setflags, d, n, m, shift_t, shift_n);
}

static Std_ReturnType exec_and_reg_t2(TargetCoreType *core, uint32 insn)
{
	return and_reg_t2(core, FALSE, t32w_AND_r_T2_Rd(insn), t32w_AND_r_T2_Rn(insn), t32w_AND_r_T2_Rm(insn),
		t32w_AND_r_T2_stype(insn), thumb_imm5(t32w_AND_r_T2_imm3(insn), t32w_AND_r_T2_imm2(insn)));
}

static Std_ReturnType exec_ands_reg_t2(TargetCoreType *core, uint32 insn)
{
	return and_reg_t2(core, TRUE, t32w_ANDS_r_T2_Rd(insn), t32w_ANDS_r_T2_Rn(insn), t32w_ANDS_r_T2_Rm(insn),
		t32w_ANDS_r_T2_stype(insn), thumb_imm5(t32w_ANDS_r_T2_imm3(insn), t32w_ANDS_r_T2_imm2(insn)));
}

static Std_ReturnType exec_and_reg_t2_rrx(TargetCoreType *core, uint32 insn)
{
	return and_reg_t2(core, FALSE, t32w_AND_r_T2_RRX_Rd(insn), t32w_AND_r_T2_RRX_Rn(insn), t32w_AND_r_T2_RRX_Rm(insn), 3U, 0U);
}

static Std_ReturnType exec_ands_reg_t2_rrx(TargetCoreType *core, uint32 insn)
{
	return and_reg_t2(core, TRUE, t32w_ANDS_r_T2_RRX_Rd(insn), t32w_ANDS_r_T2_RRX_Rn(insn), t32w_ANDS_r_T2_RRX_Rm(insn), 3U, 0U);
}

/* MOV T3: MOV{S}<c>.W <Rd>, <Rm>{, <shift>} (with a shift: LSL/LSR/ASR/ROR/RRX immediate)
 *     d = UInt(Rd); m = UInt(Rm); setflags = (S == '1');
 *     (shift_t, shift_n) = DecodeImmShift(type, imm3:imm2);
 *     without a shift: if setflags && (d IN {13,15} || m IN {13,15}) then UNPREDICTABLE;
 *                      if !setflags && (d == 15 || m == 15 || (d == 13 && m == 13)) then UNPREDICTABLE;
 *     with a shift:    if d IN {13,15} || m IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType mov_reg_t3(TargetCoreType *core, bool setflags, uint32 d, uint32 m, uint32 type, uint32 imm5)
{
	SRType shift_t;
	uint32 shift_n;
	bool unpredictable;

	DecodeImmShift(type, imm5, &shift_t, &shift_n);
	if ((shift_t == SRType_LSL) && (shift_n == 0U)) {
		if (setflags) {
			unpredictable = thumb_sp_or_pc(d) || thumb_sp_or_pc(m);
		}
		else {
			unpredictable = (d == 15U) || (m == 15U) || ((d == 13U) && (m == 13U));
		}
	}
	else {
		unpredictable = thumb_sp_or_pc(d) || thumb_sp_or_pc(m);
	}
	if (unpredictable) {
		return STD_E_INVALID;
	}
	return thumb_dp_reg(core, arm_pseudo_mov_reg, "MOV", setflags, d, THUMB_NO_REG, m, shift_t, shift_n);
}

static Std_ReturnType exec_mov_reg_t3(TargetCoreType *core, uint32 insn)
{
	return mov_reg_t3(core, FALSE, t32w_MOV_r_T3_Rd(insn), t32w_MOV_r_T3_Rm(insn),
		t32w_MOV_r_T3_stype(insn), thumb_imm5(t32w_MOV_r_T3_imm3(insn), t32w_MOV_r_T3_imm2(insn)));
}

static Std_ReturnType exec_movs_reg_t3(TargetCoreType *core, uint32 insn)
{
	return mov_reg_t3(core, TRUE, t32w_MOVS_r_T3_Rd(insn), t32w_MOVS_r_T3_Rm(insn),
		t32w_MOVS_r_T3_stype(insn), thumb_imm5(t32w_MOVS_r_T3_imm3(insn), t32w_MOVS_r_T3_imm2(insn)));
}

static Std_ReturnType exec_mov_reg_t3_rrx(TargetCoreType *core, uint32 insn)
{
	return mov_reg_t3(core, FALSE, t32w_MOV_r_T3_RRX_Rd(insn), t32w_MOV_r_T3_RRX_Rm(insn), 3U, 0U);
}

static Std_ReturnType exec_movs_reg_t3_rrx(TargetCoreType *core, uint32 insn)
{
	return mov_reg_t3(core, TRUE, t32w_MOVS_r_T3_RRX_Rd(insn), t32w_MOVS_r_T3_RRX_Rm(insn), 3U, 0U);
}

/* MOV (register-shifted register) T2: LSL/LSR/ASR/ROR{S}<c>.W <Rd>, <Rm>, <Rs>
 *     d = UInt(Rd); m = UInt(Rm); s = UInt(Rs); setflags = (S == '1'); shift_t = DecodeRegShift(type);
 *     if d IN {13,15} || m IN {13,15} || s IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType mov_rsr_t2(TargetCoreType *core, bool setflags, uint32 d, uint32 m, uint32 s, uint32 type)
{
	if (thumb_sp_or_pc(d) || thumb_sp_or_pc(m) || thumb_sp_or_pc(s)) {
		return STD_E_INVALID;
	}
	return thumb_mov_rsr(core, "MOV", setflags, d, m, s, DecodeRegShift(type));
}

static Std_ReturnType exec_mov_rsr_t2(TargetCoreType *core, uint32 insn)
{
	return mov_rsr_t2(core, FALSE, t32w_MOV_rr_T2_Rd(insn), t32w_MOV_rr_T2_Rm(insn), t32w_MOV_rr_T2_Rs(insn), t32w_MOV_rr_T2_stype(insn));
}

static Std_ReturnType exec_movs_rsr_t2(TargetCoreType *core, uint32 insn)
{
	return mov_rsr_t2(core, TRUE, t32w_MOVS_rr_T2_Rd(insn), t32w_MOVS_rr_T2_Rm(insn), t32w_MOVS_rr_T2_Rs(insn), t32w_MOVS_rr_T2_stype(insn));
}

/*
 * Data processing, immediate: the other instructions
 */
/* TST T1: TST<c> <Rn>, #<const>
 *     n = UInt(Rn); (imm32, carry) = ThumbExpandImm_C(i:imm3:imm8, APSR.C);
 *     if n IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType tst_imm_t1(TargetCoreType *core, uint32 n, uint32 imm12)
{
	bool carry;
	uint32 imm32;

	if (thumb_sp_or_pc(n)) {
		return STD_E_INVALID;
	}
	imm32 = ThumbExpandImm_C(imm12, ApsrFlags(ArmAPSR(core)).carry, &carry);
	return thumb_dp_imm(core, arm_pseudo_tst_imm, "TST", TRUE, THUMB_NO_REG, n, imm32, carry);
}

static Std_ReturnType exec_tst_imm_t1(TargetCoreType *core, uint32 insn)
{
	return tst_imm_t1(core, t32w_TST_i_T1_Rn(insn),
		thumb_imm12(t32w_TST_i_T1_i(insn), t32w_TST_i_T1_imm3(insn), t32w_TST_i_T1_imm8(insn)));
}

/* TEQ T1: TEQ<c> <Rn>, #<const>
 *     n = UInt(Rn); (imm32, carry) = ThumbExpandImm_C(i:imm3:imm8, APSR.C);
 *     if n IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType teq_imm_t1(TargetCoreType *core, uint32 n, uint32 imm12)
{
	bool carry;
	uint32 imm32;

	if (thumb_sp_or_pc(n)) {
		return STD_E_INVALID;
	}
	imm32 = ThumbExpandImm_C(imm12, ApsrFlags(ArmAPSR(core)).carry, &carry);
	return thumb_dp_imm(core, arm_pseudo_teq_imm, "TEQ", TRUE, THUMB_NO_REG, n, imm32, carry);
}

static Std_ReturnType exec_teq_imm_t1(TargetCoreType *core, uint32 insn)
{
	return teq_imm_t1(core, t32w_TEQ_i_T1_Rn(insn),
		thumb_imm12(t32w_TEQ_i_T1_i(insn), t32w_TEQ_i_T1_imm3(insn), t32w_TEQ_i_T1_imm8(insn)));
}

/* CMN T1: CMN<c> <Rn>, #<const>
 *     n = UInt(Rn); imm32 = ThumbExpandImm(i:imm3:imm8);
 *     if n == 15 then UNPREDICTABLE; */
static Std_ReturnType cmn_imm_t1(TargetCoreType *core, uint32 n, uint32 imm12)
{
	if (n == 15U) {
		return STD_E_INVALID;
	}
	return thumb_dp_imm(core, arm_pseudo_cmn_imm, "CMN", TRUE, THUMB_NO_REG, n, ThumbExpandImm(imm12), FALSE);
}

static Std_ReturnType exec_cmn_imm_t1(TargetCoreType *core, uint32 insn)
{
	return cmn_imm_t1(core, t32w_CMN_i_T1_Rn(insn),
		thumb_imm12(t32w_CMN_i_T1_i(insn), t32w_CMN_i_T1_imm3(insn), t32w_CMN_i_T1_imm8(insn)));
}

/* BIC T1: BIC{S}<c> <Rd>, <Rn>, #<const>
 *     d = UInt(Rd); n = UInt(Rn); setflags = (S == '1'); (imm32, carry) = ThumbExpandImm_C(i:imm3:imm8, APSR.C);
 *     if d IN {13,15} || n IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType bic_imm_t1(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 imm12)
{
	bool carry;
	uint32 imm32;

	if (thumb_sp_or_pc(d) || thumb_sp_or_pc(n)) {
		return STD_E_INVALID;
	}
	imm32 = ThumbExpandImm_C(imm12, ApsrFlags(ArmAPSR(core)).carry, &carry);
	return thumb_dp_imm(core, arm_pseudo_bic_imm, "BIC", setflags, d, n, imm32, carry);
}

static Std_ReturnType exec_bic_imm_t1(TargetCoreType *core, uint32 insn)
{
	return bic_imm_t1(core, FALSE, t32w_BIC_i_T1_Rd(insn), t32w_BIC_i_T1_Rn(insn),
		thumb_imm12(t32w_BIC_i_T1_i(insn), t32w_BIC_i_T1_imm3(insn), t32w_BIC_i_T1_imm8(insn)));
}

static Std_ReturnType exec_bics_imm_t1(TargetCoreType *core, uint32 insn)
{
	return bic_imm_t1(core, TRUE, t32w_BICS_i_T1_Rd(insn), t32w_BICS_i_T1_Rn(insn),
		thumb_imm12(t32w_BICS_i_T1_i(insn), t32w_BICS_i_T1_imm3(insn), t32w_BICS_i_T1_imm8(insn)));
}

/* ORR T1: ORR{S}<c> <Rd>, <Rn>, #<const>
 *     if Rn == '1111' then SEE MOV (immediate);
 *     d = UInt(Rd); n = UInt(Rn); setflags = (S == '1'); (imm32, carry) = ThumbExpandImm_C(i:imm3:imm8, APSR.C);
 *     if d IN {13,15} || n == 13 then UNPREDICTABLE; */
static Std_ReturnType orr_imm_t1(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 imm12)
{
	bool carry;
	uint32 imm32;

	if (thumb_sp_or_pc(d) || (n == 13U)) {
		return STD_E_INVALID;
	}
	imm32 = ThumbExpandImm_C(imm12, ApsrFlags(ArmAPSR(core)).carry, &carry);
	return thumb_dp_imm(core, arm_pseudo_orr_imm, "ORR", setflags, d, n, imm32, carry);
}

static Std_ReturnType exec_orr_imm_t1(TargetCoreType *core, uint32 insn)
{
	return orr_imm_t1(core, FALSE, t32w_ORR_i_T1_Rd(insn), t32w_ORR_i_T1_Rn(insn),
		thumb_imm12(t32w_ORR_i_T1_i(insn), t32w_ORR_i_T1_imm3(insn), t32w_ORR_i_T1_imm8(insn)));
}

static Std_ReturnType exec_orrs_imm_t1(TargetCoreType *core, uint32 insn)
{
	return orr_imm_t1(core, TRUE, t32w_ORRS_i_T1_Rd(insn), t32w_ORRS_i_T1_Rn(insn),
		thumb_imm12(t32w_ORRS_i_T1_i(insn), t32w_ORRS_i_T1_imm3(insn), t32w_ORRS_i_T1_imm8(insn)));
}

/* ORN T1: ORN{S}<c> <Rd>, <Rn>, #<const>
 *     if Rn == '1111' then SEE MVN (immediate);
 *     d = UInt(Rd); n = UInt(Rn); setflags = (S == '1'); (imm32, carry) = ThumbExpandImm_C(i:imm3:imm8, APSR.C);
 *     if d IN {13,15} || n == 13 then UNPREDICTABLE; */
static Std_ReturnType orn_imm_t1(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 imm12)
{
	bool carry;
	uint32 imm32;

	if (thumb_sp_or_pc(d) || (n == 13U)) {
		return STD_E_INVALID;
	}
	imm32 = ThumbExpandImm_C(imm12, ApsrFlags(ArmAPSR(core)).carry, &carry);
	return thumb_dp_imm(core, arm_pseudo_orn_imm, "ORN", setflags, d, n, imm32, carry);
}

static Std_ReturnType exec_orn_imm_t1(TargetCoreType *core, uint32 insn)
{
	return orn_imm_t1(core, FALSE, t32w_ORN_i_T1_Rd(insn), t32w_ORN_i_T1_Rn(insn),
		thumb_imm12(t32w_ORN_i_T1_i(insn), t32w_ORN_i_T1_imm3(insn), t32w_ORN_i_T1_imm8(insn)));
}

static Std_ReturnType exec_orns_imm_t1(TargetCoreType *core, uint32 insn)
{
	return orn_imm_t1(core, TRUE, t32w_ORNS_i_T1_Rd(insn), t32w_ORNS_i_T1_Rn(insn),
		thumb_imm12(t32w_ORNS_i_T1_i(insn), t32w_ORNS_i_T1_imm3(insn), t32w_ORNS_i_T1_imm8(insn)));
}

/* EOR T1: EOR{S}<c> <Rd>, <Rn>, #<const>
 *     if Rd == '1111' && S == '1' then SEE TEQ (immediate);
 *     d = UInt(Rd); n = UInt(Rn); setflags = (S == '1'); (imm32, carry) = ThumbExpandImm_C(i:imm3:imm8, APSR.C);
 *     if d == 13 || (d == 15 && S == '0') || n IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType eor_imm_t1(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 imm12)
{
	bool carry;
	uint32 imm32;

	if ((d == 13U) || (d == 15U) || thumb_sp_or_pc(n)) {
		return STD_E_INVALID;
	}
	imm32 = ThumbExpandImm_C(imm12, ApsrFlags(ArmAPSR(core)).carry, &carry);
	return thumb_dp_imm(core, arm_pseudo_eor_imm, "EOR", setflags, d, n, imm32, carry);
}

static Std_ReturnType exec_eor_imm_t1(TargetCoreType *core, uint32 insn)
{
	return eor_imm_t1(core, FALSE, t32w_EOR_i_T1_Rd(insn), t32w_EOR_i_T1_Rn(insn),
		thumb_imm12(t32w_EOR_i_T1_i(insn), t32w_EOR_i_T1_imm3(insn), t32w_EOR_i_T1_imm8(insn)));
}

static Std_ReturnType exec_eors_imm_t1(TargetCoreType *core, uint32 insn)
{
	return eor_imm_t1(core, TRUE, t32w_EORS_i_T1_Rd(insn), t32w_EORS_i_T1_Rn(insn),
		thumb_imm12(t32w_EORS_i_T1_i(insn), t32w_EORS_i_T1_imm3(insn), t32w_EORS_i_T1_imm8(insn)));
}

/* MVN T1: MVN{S}<c> <Rd>, #<const>
 *     d = UInt(Rd); setflags = (S == '1'); (imm32, carry) = ThumbExpandImm_C(i:imm3:imm8, APSR.C);
 *     if d IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType mvn_imm_t1(TargetCoreType *core, bool setflags, uint32 d, uint32 imm12)
{
	bool carry;
	uint32 imm32;

	if (thumb_sp_or_pc(d)) {
		return STD_E_INVALID;
	}
	imm32 = ThumbExpandImm_C(imm12, ApsrFlags(ArmAPSR(core)).carry, &carry);
	return thumb_dp_imm(core, arm_pseudo_mvn_imm, "MVN", setflags, d, THUMB_NO_REG, imm32, carry);
}

static Std_ReturnType exec_mvn_imm_t1(TargetCoreType *core, uint32 insn)
{
	return mvn_imm_t1(core, FALSE, t32w_MVN_i_T1_Rd(insn),
		thumb_imm12(t32w_MVN_i_T1_i(insn), t32w_MVN_i_T1_imm3(insn), t32w_MVN_i_T1_imm8(insn)));
}

static Std_ReturnType exec_mvns_imm_t1(TargetCoreType *core, uint32 insn)
{
	return mvn_imm_t1(core, TRUE, t32w_MVNS_i_T1_Rd(insn),
		thumb_imm12(t32w_MVNS_i_T1_i(insn), t32w_MVNS_i_T1_imm3(insn), t32w_MVNS_i_T1_imm8(insn)));
}

/* ADC T1: ADC{S}<c> <Rd>, <Rn>, #<const>
 *     d = UInt(Rd); n = UInt(Rn); setflags = (S == '1'); imm32 = ThumbExpandImm(i:imm3:imm8);
 *     if d IN {13,15} || n IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType adc_imm_t1(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 imm12)
{
	if (thumb_sp_or_pc(d) || thumb_sp_or_pc(n)) {
		return STD_E_INVALID;
	}
	return thumb_dp_imm(core, arm_pseudo_adc_imm, "ADC", setflags, d, n, ThumbExpandImm(imm12), FALSE);
}

static Std_ReturnType exec_adc_imm_t1(TargetCoreType *core, uint32 insn)
{
	return adc_imm_t1(core, FALSE, t32w_ADC_i_T1_Rd(insn), t32w_ADC_i_T1_Rn(insn),
		thumb_imm12(t32w_ADC_i_T1_i(insn), t32w_ADC_i_T1_imm3(insn), t32w_ADC_i_T1_imm8(insn)));
}

static Std_ReturnType exec_adcs_imm_t1(TargetCoreType *core, uint32 insn)
{
	return adc_imm_t1(core, TRUE, t32w_ADCS_i_T1_Rd(insn), t32w_ADCS_i_T1_Rn(insn),
		thumb_imm12(t32w_ADCS_i_T1_i(insn), t32w_ADCS_i_T1_imm3(insn), t32w_ADCS_i_T1_imm8(insn)));
}

/* SBC T1: SBC{S}<c> <Rd>, <Rn>, #<const>
 *     d = UInt(Rd); n = UInt(Rn); setflags = (S == '1'); imm32 = ThumbExpandImm(i:imm3:imm8);
 *     if d IN {13,15} || n IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType sbc_imm_t1(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 imm12)
{
	if (thumb_sp_or_pc(d) || thumb_sp_or_pc(n)) {
		return STD_E_INVALID;
	}
	return thumb_dp_imm(core, arm_pseudo_sbc_imm, "SBC", setflags, d, n, ThumbExpandImm(imm12), FALSE);
}

static Std_ReturnType exec_sbc_imm_t1(TargetCoreType *core, uint32 insn)
{
	return sbc_imm_t1(core, FALSE, t32w_SBC_i_T1_Rd(insn), t32w_SBC_i_T1_Rn(insn),
		thumb_imm12(t32w_SBC_i_T1_i(insn), t32w_SBC_i_T1_imm3(insn), t32w_SBC_i_T1_imm8(insn)));
}

static Std_ReturnType exec_sbcs_imm_t1(TargetCoreType *core, uint32 insn)
{
	return sbc_imm_t1(core, TRUE, t32w_SBCS_i_T1_Rd(insn), t32w_SBCS_i_T1_Rn(insn),
		thumb_imm12(t32w_SBCS_i_T1_i(insn), t32w_SBCS_i_T1_imm3(insn), t32w_SBCS_i_T1_imm8(insn)));
}

/* RSB T2: RSB{S}<c>.W <Rd>, <Rn>, #<const>
 *     d = UInt(Rd); n = UInt(Rn); setflags = (S == '1'); imm32 = ThumbExpandImm(i:imm3:imm8);
 *     if d IN {13,15} || n IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType rsb_imm_t2(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 imm12)
{
	if (thumb_sp_or_pc(d) || thumb_sp_or_pc(n)) {
		return STD_E_INVALID;
	}
	return thumb_dp_imm(core, arm_pseudo_rsb_imm, "RSB", setflags, d, n, ThumbExpandImm(imm12), FALSE);
}

static Std_ReturnType exec_rsb_imm_t2(TargetCoreType *core, uint32 insn)
{
	return rsb_imm_t2(core, FALSE, t32w_RSB_i_T2_Rd(insn), t32w_RSB_i_T2_Rn(insn),
		thumb_imm12(t32w_RSB_i_T2_i(insn), t32w_RSB_i_T2_imm3(insn), t32w_RSB_i_T2_imm8(insn)));
}

static Std_ReturnType exec_rsbs_imm_t2(TargetCoreType *core, uint32 insn)
{
	return rsb_imm_t2(core, TRUE, t32w_RSBS_i_T2_Rd(insn), t32w_RSBS_i_T2_Rn(insn),
		thumb_imm12(t32w_RSBS_i_T2_i(insn), t32w_RSBS_i_T2_imm3(insn), t32w_RSBS_i_T2_imm8(insn)));
}

/*
 * Data processing, register: the other instructions
 */
/* TST T2: TST<c>.W <Rn>, <Rm>{, <shift>}
 *     n = UInt(Rn); m = UInt(Rm); (shift_t, shift_n) = DecodeImmShift(type, imm3:imm2);
 *     if n IN {13,15} || m IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType tst_reg_t2(TargetCoreType *core, uint32 n, uint32 m, uint32 type, uint32 imm5)
{
	SRType shift_t;
	uint32 shift_n;

	if (thumb_sp_or_pc(n) || thumb_sp_or_pc(m)) {
		return STD_E_INVALID;
	}
	DecodeImmShift(type, imm5, &shift_t, &shift_n);
	return thumb_dp_reg(core, arm_pseudo_tst_reg, "TST", TRUE, THUMB_NO_REG, n, m, shift_t, shift_n);
}

static Std_ReturnType exec_tst_reg_t2(TargetCoreType *core, uint32 insn)
{
	return tst_reg_t2(core, t32w_TST_r_T2_Rn(insn), t32w_TST_r_T2_Rm(insn),
		t32w_TST_r_T2_stype(insn), thumb_imm5(t32w_TST_r_T2_imm3(insn), t32w_TST_r_T2_imm2(insn)));
}

static Std_ReturnType exec_tst_reg_t2_rrx(TargetCoreType *core, uint32 insn)
{
	return tst_reg_t2(core, t32w_TST_r_T2_RRX_Rn(insn), t32w_TST_r_T2_RRX_Rm(insn),
		3U, 0U);
}

/* TEQ T1: TEQ<c> <Rn>, <Rm>{, <shift>}
 *     n = UInt(Rn); m = UInt(Rm); (shift_t, shift_n) = DecodeImmShift(type, imm3:imm2);
 *     if n IN {13,15} || m IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType teq_reg_t1(TargetCoreType *core, uint32 n, uint32 m, uint32 type, uint32 imm5)
{
	SRType shift_t;
	uint32 shift_n;

	if (thumb_sp_or_pc(n) || thumb_sp_or_pc(m)) {
		return STD_E_INVALID;
	}
	DecodeImmShift(type, imm5, &shift_t, &shift_n);
	return thumb_dp_reg(core, arm_pseudo_teq_reg, "TEQ", TRUE, THUMB_NO_REG, n, m, shift_t, shift_n);
}

static Std_ReturnType exec_teq_reg_t1(TargetCoreType *core, uint32 insn)
{
	return teq_reg_t1(core, t32w_TEQ_r_T1_Rn(insn), t32w_TEQ_r_T1_Rm(insn),
		t32w_TEQ_r_T1_stype(insn), thumb_imm5(t32w_TEQ_r_T1_imm3(insn), t32w_TEQ_r_T1_imm2(insn)));
}

static Std_ReturnType exec_teq_reg_t1_rrx(TargetCoreType *core, uint32 insn)
{
	return teq_reg_t1(core, t32w_TEQ_r_T1_RRX_Rn(insn), t32w_TEQ_r_T1_RRX_Rm(insn),
		3U, 0U);
}

/* CMN T2: CMN<c>.W <Rn>, <Rm>{, <shift>}
 *     n = UInt(Rn); m = UInt(Rm); (shift_t, shift_n) = DecodeImmShift(type, imm3:imm2);
 *     if n == 15 || m IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType cmn_reg_t2(TargetCoreType *core, uint32 n, uint32 m, uint32 type, uint32 imm5)
{
	SRType shift_t;
	uint32 shift_n;

	if ((n == 15U) || thumb_sp_or_pc(m)) {
		return STD_E_INVALID;
	}
	DecodeImmShift(type, imm5, &shift_t, &shift_n);
	return thumb_dp_reg(core, arm_pseudo_cmn_reg, "CMN", TRUE, THUMB_NO_REG, n, m, shift_t, shift_n);
}

static Std_ReturnType exec_cmn_reg_t2(TargetCoreType *core, uint32 insn)
{
	return cmn_reg_t2(core, t32w_CMN_r_T2_Rn(insn), t32w_CMN_r_T2_Rm(insn),
		t32w_CMN_r_T2_stype(insn), thumb_imm5(t32w_CMN_r_T2_imm3(insn), t32w_CMN_r_T2_imm2(insn)));
}

static Std_ReturnType exec_cmn_reg_t2_rrx(TargetCoreType *core, uint32 insn)
{
	return cmn_reg_t2(core, t32w_CMN_r_T2_RRX_Rn(insn), t32w_CMN_r_T2_RRX_Rm(insn),
		3U, 0U);
}

/* BIC T2: BIC{S}<c>.W <Rd>, <Rn>, <Rm>{, <shift>}
 *     d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); setflags = (S == '1'); (shift_t, shift_n) = DecodeImmShift(type, imm3:imm2);
 *     if d IN {13,15} || n IN {13,15} || m IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType bic_reg_t2(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 m, uint32 type, uint32 imm5)
{
	SRType shift_t;
	uint32 shift_n;

	if (thumb_sp_or_pc(d) || thumb_sp_or_pc(n) || thumb_sp_or_pc(m)) {
		return STD_E_INVALID;
	}
	DecodeImmShift(type, imm5, &shift_t, &shift_n);
	return thumb_dp_reg(core, arm_pseudo_bic_reg, "BIC", setflags, d, n, m, shift_t, shift_n);
}

static Std_ReturnType exec_bic_reg_t2(TargetCoreType *core, uint32 insn)
{
	return bic_reg_t2(core, FALSE, t32w_BIC_r_T2_Rd(insn), t32w_BIC_r_T2_Rn(insn), t32w_BIC_r_T2_Rm(insn),
		t32w_BIC_r_T2_stype(insn), thumb_imm5(t32w_BIC_r_T2_imm3(insn), t32w_BIC_r_T2_imm2(insn)));
}

static Std_ReturnType exec_bic_reg_t2_rrx(TargetCoreType *core, uint32 insn)
{
	return bic_reg_t2(core, FALSE, t32w_BIC_r_T2_RRX_Rd(insn), t32w_BIC_r_T2_RRX_Rn(insn), t32w_BIC_r_T2_RRX_Rm(insn),
		3U, 0U);
}

static Std_ReturnType exec_bics_reg_t2(TargetCoreType *core, uint32 insn)
{
	return bic_reg_t2(core, TRUE, t32w_BICS_r_T2_Rd(insn), t32w_BICS_r_T2_Rn(insn), t32w_BICS_r_T2_Rm(insn),
		t32w_BICS_r_T2_stype(insn), thumb_imm5(t32w_BICS_r_T2_imm3(insn), t32w_BICS_r_T2_imm2(insn)));
}

static Std_ReturnType exec_bics_reg_t2_rrx(TargetCoreType *core, uint32 insn)
{
	return bic_reg_t2(core, TRUE, t32w_BICS_r_T2_RRX_Rd(insn), t32w_BICS_r_T2_RRX_Rn(insn), t32w_BICS_r_T2_RRX_Rm(insn),
		3U, 0U);
}

/* ORR T2: ORR{S}<c>.W <Rd>, <Rn>, <Rm>{, <shift>}
 *     if Rn == '1111' then SEE MOV (register);
 *     d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); setflags = (S == '1'); (shift_t, shift_n) = DecodeImmShift(type, imm3:imm2);
 *     if d IN {13,15} || n == 13 || m IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType orr_reg_t2(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 m, uint32 type, uint32 imm5)
{
	SRType shift_t;
	uint32 shift_n;

	if (thumb_sp_or_pc(d) || (n == 13U) || thumb_sp_or_pc(m)) {
		return STD_E_INVALID;
	}
	DecodeImmShift(type, imm5, &shift_t, &shift_n);
	return thumb_dp_reg(core, arm_pseudo_orr_reg, "ORR", setflags, d, n, m, shift_t, shift_n);
}

static Std_ReturnType exec_orr_reg_t2(TargetCoreType *core, uint32 insn)
{
	return orr_reg_t2(core, FALSE, t32w_ORR_r_T2_Rd(insn), t32w_ORR_r_T2_Rn(insn), t32w_ORR_r_T2_Rm(insn),
		t32w_ORR_r_T2_stype(insn), thumb_imm5(t32w_ORR_r_T2_imm3(insn), t32w_ORR_r_T2_imm2(insn)));
}

static Std_ReturnType exec_orr_reg_t2_rrx(TargetCoreType *core, uint32 insn)
{
	return orr_reg_t2(core, FALSE, t32w_ORR_r_T2_RRX_Rd(insn), t32w_ORR_r_T2_RRX_Rn(insn), t32w_ORR_r_T2_RRX_Rm(insn),
		3U, 0U);
}

static Std_ReturnType exec_orrs_reg_t2(TargetCoreType *core, uint32 insn)
{
	return orr_reg_t2(core, TRUE, t32w_ORRS_r_T2_Rd(insn), t32w_ORRS_r_T2_Rn(insn), t32w_ORRS_r_T2_Rm(insn),
		t32w_ORRS_r_T2_stype(insn), thumb_imm5(t32w_ORRS_r_T2_imm3(insn), t32w_ORRS_r_T2_imm2(insn)));
}

static Std_ReturnType exec_orrs_reg_t2_rrx(TargetCoreType *core, uint32 insn)
{
	return orr_reg_t2(core, TRUE, t32w_ORRS_r_T2_RRX_Rd(insn), t32w_ORRS_r_T2_RRX_Rn(insn), t32w_ORRS_r_T2_RRX_Rm(insn),
		3U, 0U);
}

/* ORN T1: ORN{S}<c> <Rd>, <Rn>, <Rm>{, <shift>}
 *     if Rn == '1111' then SEE MVN (register);
 *     d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); setflags = (S == '1'); (shift_t, shift_n) = DecodeImmShift(type, imm3:imm2);
 *     if d IN {13,15} || n == 13 || m IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType orn_reg_t1(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 m, uint32 type, uint32 imm5)
{
	SRType shift_t;
	uint32 shift_n;

	if (thumb_sp_or_pc(d) || (n == 13U) || thumb_sp_or_pc(m)) {
		return STD_E_INVALID;
	}
	DecodeImmShift(type, imm5, &shift_t, &shift_n);
	return thumb_dp_reg(core, arm_pseudo_orn_reg, "ORN", setflags, d, n, m, shift_t, shift_n);
}

static Std_ReturnType exec_orn_reg_t1(TargetCoreType *core, uint32 insn)
{
	return orn_reg_t1(core, FALSE, t32w_ORN_r_T1_Rd(insn), t32w_ORN_r_T1_Rn(insn), t32w_ORN_r_T1_Rm(insn),
		t32w_ORN_r_T1_stype(insn), thumb_imm5(t32w_ORN_r_T1_imm3(insn), t32w_ORN_r_T1_imm2(insn)));
}

static Std_ReturnType exec_orn_reg_t1_rrx(TargetCoreType *core, uint32 insn)
{
	return orn_reg_t1(core, FALSE, t32w_ORN_r_T1_RRX_Rd(insn), t32w_ORN_r_T1_RRX_Rn(insn), t32w_ORN_r_T1_RRX_Rm(insn),
		3U, 0U);
}

static Std_ReturnType exec_orns_reg_t1(TargetCoreType *core, uint32 insn)
{
	return orn_reg_t1(core, TRUE, t32w_ORNS_r_T1_Rd(insn), t32w_ORNS_r_T1_Rn(insn), t32w_ORNS_r_T1_Rm(insn),
		t32w_ORNS_r_T1_stype(insn), thumb_imm5(t32w_ORNS_r_T1_imm3(insn), t32w_ORNS_r_T1_imm2(insn)));
}

static Std_ReturnType exec_orns_reg_t1_rrx(TargetCoreType *core, uint32 insn)
{
	return orn_reg_t1(core, TRUE, t32w_ORNS_r_T1_RRX_Rd(insn), t32w_ORNS_r_T1_RRX_Rn(insn), t32w_ORNS_r_T1_RRX_Rm(insn),
		3U, 0U);
}

/* EOR T2: EOR{S}<c>.W <Rd>, <Rn>, <Rm>{, <shift>}
 *     if Rd == '1111' && S == '1' then SEE TEQ (register);
 *     d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); setflags = (S == '1'); (shift_t, shift_n) = DecodeImmShift(type, imm3:imm2);
 *     if d == 13 || (d == 15 && S == '0') || n IN {13,15} || m IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType eor_reg_t2(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 m, uint32 type, uint32 imm5)
{
	SRType shift_t;
	uint32 shift_n;

	if ((d == 13U) || (d == 15U) || thumb_sp_or_pc(n) || thumb_sp_or_pc(m)) {
		return STD_E_INVALID;
	}
	DecodeImmShift(type, imm5, &shift_t, &shift_n);
	return thumb_dp_reg(core, arm_pseudo_eor_reg, "EOR", setflags, d, n, m, shift_t, shift_n);
}

static Std_ReturnType exec_eor_reg_t2(TargetCoreType *core, uint32 insn)
{
	return eor_reg_t2(core, FALSE, t32w_EOR_r_T2_Rd(insn), t32w_EOR_r_T2_Rn(insn), t32w_EOR_r_T2_Rm(insn),
		t32w_EOR_r_T2_stype(insn), thumb_imm5(t32w_EOR_r_T2_imm3(insn), t32w_EOR_r_T2_imm2(insn)));
}

static Std_ReturnType exec_eor_reg_t2_rrx(TargetCoreType *core, uint32 insn)
{
	return eor_reg_t2(core, FALSE, t32w_EOR_r_T2_RRX_Rd(insn), t32w_EOR_r_T2_RRX_Rn(insn), t32w_EOR_r_T2_RRX_Rm(insn),
		3U, 0U);
}

static Std_ReturnType exec_eors_reg_t2(TargetCoreType *core, uint32 insn)
{
	return eor_reg_t2(core, TRUE, t32w_EORS_r_T2_Rd(insn), t32w_EORS_r_T2_Rn(insn), t32w_EORS_r_T2_Rm(insn),
		t32w_EORS_r_T2_stype(insn), thumb_imm5(t32w_EORS_r_T2_imm3(insn), t32w_EORS_r_T2_imm2(insn)));
}

static Std_ReturnType exec_eors_reg_t2_rrx(TargetCoreType *core, uint32 insn)
{
	return eor_reg_t2(core, TRUE, t32w_EORS_r_T2_RRX_Rd(insn), t32w_EORS_r_T2_RRX_Rn(insn), t32w_EORS_r_T2_RRX_Rm(insn),
		3U, 0U);
}

/* MVN T2: MVN{S}<c>.W <Rd>, <Rm>{, <shift>}
 *     d = UInt(Rd); m = UInt(Rm); setflags = (S == '1'); (shift_t, shift_n) = DecodeImmShift(type, imm3:imm2);
 *     if d IN {13,15} || m IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType mvn_reg_t2(TargetCoreType *core, bool setflags, uint32 d, uint32 m, uint32 type, uint32 imm5)
{
	SRType shift_t;
	uint32 shift_n;

	if (thumb_sp_or_pc(d) || thumb_sp_or_pc(m)) {
		return STD_E_INVALID;
	}
	DecodeImmShift(type, imm5, &shift_t, &shift_n);
	return thumb_dp_reg(core, arm_pseudo_mvn_reg, "MVN", setflags, d, THUMB_NO_REG, m, shift_t, shift_n);
}

static Std_ReturnType exec_mvn_reg_t2(TargetCoreType *core, uint32 insn)
{
	return mvn_reg_t2(core, FALSE, t32w_MVN_r_T2_Rd(insn), t32w_MVN_r_T2_Rm(insn),
		t32w_MVN_r_T2_stype(insn), thumb_imm5(t32w_MVN_r_T2_imm3(insn), t32w_MVN_r_T2_imm2(insn)));
}

static Std_ReturnType exec_mvn_reg_t2_rrx(TargetCoreType *core, uint32 insn)
{
	return mvn_reg_t2(core, FALSE, t32w_MVN_r_T2_RRX_Rd(insn), t32w_MVN_r_T2_RRX_Rm(insn),
		3U, 0U);
}

static Std_ReturnType exec_mvns_reg_t2(TargetCoreType *core, uint32 insn)
{
	return mvn_reg_t2(core, TRUE, t32w_MVNS_r_T2_Rd(insn), t32w_MVNS_r_T2_Rm(insn),
		t32w_MVNS_r_T2_stype(insn), thumb_imm5(t32w_MVNS_r_T2_imm3(insn), t32w_MVNS_r_T2_imm2(insn)));
}

static Std_ReturnType exec_mvns_reg_t2_rrx(TargetCoreType *core, uint32 insn)
{
	return mvn_reg_t2(core, TRUE, t32w_MVNS_r_T2_RRX_Rd(insn), t32w_MVNS_r_T2_RRX_Rm(insn),
		3U, 0U);
}

/* ADC T2: ADC{S}<c>.W <Rd>, <Rn>, <Rm>{, <shift>}
 *     d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); setflags = (S == '1'); (shift_t, shift_n) = DecodeImmShift(type, imm3:imm2);
 *     if d IN {13,15} || n IN {13,15} || m IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType adc_reg_t2(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 m, uint32 type, uint32 imm5)
{
	SRType shift_t;
	uint32 shift_n;

	if (thumb_sp_or_pc(d) || thumb_sp_or_pc(n) || thumb_sp_or_pc(m)) {
		return STD_E_INVALID;
	}
	DecodeImmShift(type, imm5, &shift_t, &shift_n);
	return thumb_dp_reg(core, arm_pseudo_adc_reg, "ADC", setflags, d, n, m, shift_t, shift_n);
}

static Std_ReturnType exec_adc_reg_t2(TargetCoreType *core, uint32 insn)
{
	return adc_reg_t2(core, FALSE, t32w_ADC_r_T2_Rd(insn), t32w_ADC_r_T2_Rn(insn), t32w_ADC_r_T2_Rm(insn),
		t32w_ADC_r_T2_stype(insn), thumb_imm5(t32w_ADC_r_T2_imm3(insn), t32w_ADC_r_T2_imm2(insn)));
}

static Std_ReturnType exec_adc_reg_t2_rrx(TargetCoreType *core, uint32 insn)
{
	return adc_reg_t2(core, FALSE, t32w_ADC_r_T2_RRX_Rd(insn), t32w_ADC_r_T2_RRX_Rn(insn), t32w_ADC_r_T2_RRX_Rm(insn),
		3U, 0U);
}

static Std_ReturnType exec_adcs_reg_t2(TargetCoreType *core, uint32 insn)
{
	return adc_reg_t2(core, TRUE, t32w_ADCS_r_T2_Rd(insn), t32w_ADCS_r_T2_Rn(insn), t32w_ADCS_r_T2_Rm(insn),
		t32w_ADCS_r_T2_stype(insn), thumb_imm5(t32w_ADCS_r_T2_imm3(insn), t32w_ADCS_r_T2_imm2(insn)));
}

static Std_ReturnType exec_adcs_reg_t2_rrx(TargetCoreType *core, uint32 insn)
{
	return adc_reg_t2(core, TRUE, t32w_ADCS_r_T2_RRX_Rd(insn), t32w_ADCS_r_T2_RRX_Rn(insn), t32w_ADCS_r_T2_RRX_Rm(insn),
		3U, 0U);
}

/* SBC T2: SBC{S}<c>.W <Rd>, <Rn>, <Rm>{, <shift>}
 *     d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); setflags = (S == '1'); (shift_t, shift_n) = DecodeImmShift(type, imm3:imm2);
 *     if d IN {13,15} || n IN {13,15} || m IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType sbc_reg_t2(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 m, uint32 type, uint32 imm5)
{
	SRType shift_t;
	uint32 shift_n;

	if (thumb_sp_or_pc(d) || thumb_sp_or_pc(n) || thumb_sp_or_pc(m)) {
		return STD_E_INVALID;
	}
	DecodeImmShift(type, imm5, &shift_t, &shift_n);
	return thumb_dp_reg(core, arm_pseudo_sbc_reg, "SBC", setflags, d, n, m, shift_t, shift_n);
}

static Std_ReturnType exec_sbc_reg_t2(TargetCoreType *core, uint32 insn)
{
	return sbc_reg_t2(core, FALSE, t32w_SBC_r_T2_Rd(insn), t32w_SBC_r_T2_Rn(insn), t32w_SBC_r_T2_Rm(insn),
		t32w_SBC_r_T2_stype(insn), thumb_imm5(t32w_SBC_r_T2_imm3(insn), t32w_SBC_r_T2_imm2(insn)));
}

static Std_ReturnType exec_sbc_reg_t2_rrx(TargetCoreType *core, uint32 insn)
{
	return sbc_reg_t2(core, FALSE, t32w_SBC_r_T2_RRX_Rd(insn), t32w_SBC_r_T2_RRX_Rn(insn), t32w_SBC_r_T2_RRX_Rm(insn),
		3U, 0U);
}

static Std_ReturnType exec_sbcs_reg_t2(TargetCoreType *core, uint32 insn)
{
	return sbc_reg_t2(core, TRUE, t32w_SBCS_r_T2_Rd(insn), t32w_SBCS_r_T2_Rn(insn), t32w_SBCS_r_T2_Rm(insn),
		t32w_SBCS_r_T2_stype(insn), thumb_imm5(t32w_SBCS_r_T2_imm3(insn), t32w_SBCS_r_T2_imm2(insn)));
}

static Std_ReturnType exec_sbcs_reg_t2_rrx(TargetCoreType *core, uint32 insn)
{
	return sbc_reg_t2(core, TRUE, t32w_SBCS_r_T2_RRX_Rd(insn), t32w_SBCS_r_T2_RRX_Rn(insn), t32w_SBCS_r_T2_RRX_Rm(insn),
		3U, 0U);
}

/* SUB T2: SUB{S}<c>.W <Rd>, <Rn>, <Rm>{, <shift>}
 *     if Rd == '1111' && S == '1' then SEE CMP (register);
 *     if Rn == '1101' then SEE SUB (SP minus register);
 *     d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); setflags = (S == '1'); (shift_t, shift_n) = DecodeImmShift(type, imm3:imm2);
 *     if d == 13 || (d == 15 && S == '0') || n == 15 || m IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType sub_reg_t2(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 m, uint32 type, uint32 imm5)
{
	SRType shift_t;
	uint32 shift_n;

	if ((d == 13U) || (d == 15U) || (n == 15U) || thumb_sp_or_pc(m)) {
		return STD_E_INVALID;
	}
	DecodeImmShift(type, imm5, &shift_t, &shift_n);
	return thumb_dp_reg(core, arm_pseudo_sub_reg, "SUB", setflags, d, n, m, shift_t, shift_n);
}

static Std_ReturnType exec_sub_reg_t2(TargetCoreType *core, uint32 insn)
{
	return sub_reg_t2(core, FALSE, t32w_SUB_r_T2_Rd(insn), t32w_SUB_r_T2_Rn(insn), t32w_SUB_r_T2_Rm(insn),
		t32w_SUB_r_T2_stype(insn), thumb_imm5(t32w_SUB_r_T2_imm3(insn), t32w_SUB_r_T2_imm2(insn)));
}

static Std_ReturnType exec_sub_reg_t2_rrx(TargetCoreType *core, uint32 insn)
{
	return sub_reg_t2(core, FALSE, t32w_SUB_r_T2_RRX_Rd(insn), t32w_SUB_r_T2_RRX_Rn(insn), t32w_SUB_r_T2_RRX_Rm(insn),
		3U, 0U);
}

static Std_ReturnType exec_subs_reg_t2(TargetCoreType *core, uint32 insn)
{
	return sub_reg_t2(core, TRUE, t32w_SUBS_r_T2_Rd(insn), t32w_SUBS_r_T2_Rn(insn), t32w_SUBS_r_T2_Rm(insn),
		t32w_SUBS_r_T2_stype(insn), thumb_imm5(t32w_SUBS_r_T2_imm3(insn), t32w_SUBS_r_T2_imm2(insn)));
}

static Std_ReturnType exec_subs_reg_t2_rrx(TargetCoreType *core, uint32 insn)
{
	return sub_reg_t2(core, TRUE, t32w_SUBS_r_T2_RRX_Rd(insn), t32w_SUBS_r_T2_RRX_Rn(insn), t32w_SUBS_r_T2_RRX_Rm(insn),
		3U, 0U);
}

/* RSB T1: RSB{S}<c> <Rd>, <Rn>, <Rm>{, <shift>}
 *     d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); setflags = (S == '1'); (shift_t, shift_n) = DecodeImmShift(type, imm3:imm2);
 *     if d IN {13,15} || n IN {13,15} || m IN {13,15} then UNPREDICTABLE; */
static Std_ReturnType rsb_reg_t1(TargetCoreType *core, bool setflags, uint32 d, uint32 n, uint32 m, uint32 type, uint32 imm5)
{
	SRType shift_t;
	uint32 shift_n;

	if (thumb_sp_or_pc(d) || thumb_sp_or_pc(n) || thumb_sp_or_pc(m)) {
		return STD_E_INVALID;
	}
	DecodeImmShift(type, imm5, &shift_t, &shift_n);
	return thumb_dp_reg(core, arm_pseudo_rsb_reg, "RSB", setflags, d, n, m, shift_t, shift_n);
}

static Std_ReturnType exec_rsb_reg_t1(TargetCoreType *core, uint32 insn)
{
	return rsb_reg_t1(core, FALSE, t32w_RSB_r_T1_Rd(insn), t32w_RSB_r_T1_Rn(insn), t32w_RSB_r_T1_Rm(insn),
		t32w_RSB_r_T1_stype(insn), thumb_imm5(t32w_RSB_r_T1_imm3(insn), t32w_RSB_r_T1_imm2(insn)));
}

static Std_ReturnType exec_rsb_reg_t1_rrx(TargetCoreType *core, uint32 insn)
{
	return rsb_reg_t1(core, FALSE, t32w_RSB_r_T1_RRX_Rd(insn), t32w_RSB_r_T1_RRX_Rn(insn), t32w_RSB_r_T1_RRX_Rm(insn),
		3U, 0U);
}

static Std_ReturnType exec_rsbs_reg_t1(TargetCoreType *core, uint32 insn)
{
	return rsb_reg_t1(core, TRUE, t32w_RSBS_r_T1_Rd(insn), t32w_RSBS_r_T1_Rn(insn), t32w_RSBS_r_T1_Rm(insn),
		t32w_RSBS_r_T1_stype(insn), thumb_imm5(t32w_RSBS_r_T1_imm3(insn), t32w_RSBS_r_T1_imm2(insn)));
}

static Std_ReturnType exec_rsbs_reg_t1_rrx(TargetCoreType *core, uint32 insn)
{
	return rsb_reg_t1(core, TRUE, t32w_RSBS_r_T1_RRX_Rd(insn), t32w_RSBS_r_T1_RRX_Rn(insn), t32w_RSBS_r_T1_RRX_Rm(insn),
		3U, 0U);
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

	if (thumb_in_it_not_last(core)) {
		return STD_E_INVALID;
	}
	in.instrName = "B";
	in.cond = Stm32CurrentCond(core);
	in.imm32 = SignExtend(imm25, 25U);
	return arm_pseudo_b(core, &in, &out);
}

/*
 * The target's: the hints that wait (thumb_glue.h), UDF.
 */

static Std_ReturnType exec_wfi_t2(TargetCoreType *core, uint32 insn)
{
	(void)insn;
	return thumb_wfi(core);
}

static Std_ReturnType exec_wfe_t2(TargetCoreType *core, uint32 insn)
{
	(void)insn;
	return thumb_wfe(core);
}

static Std_ReturnType exec_sev_t2(TargetCoreType *core, uint32 insn)
{
	(void)insn;
	return thumb_sev(core);
}

/* UDF: UNDEFINED (thumb_execute takes the UsageFault UNDEFINSTR). */
static Std_ReturnType exec_udf_t2(TargetCoreType *core, uint32 insn)
{
	(void)core;
	(void)insn;
	return STD_E_DECODE;
}

static const ThumbExec32 executors[T32_32_ID_NUM] = {
	[T32_32_WFI_T2] = exec_wfi_t2,
	[T32_32_WFE_T2] = exec_wfe_t2,
	[T32_32_SEV_T2] = exec_sev_t2,
	[T32_32_UDF_T2] = exec_udf_t2,
	[T32_32_ADD_i_T3] = exec_add_imm_t3,
	[T32_32_ADDS_i_T3] = exec_adds_imm_t3,
	[T32_32_ADD_i_T4] = exec_add_imm_t4,
	[T32_32_SUB_i_T3] = exec_sub_imm_t3,
	[T32_32_SUBS_i_T3] = exec_subs_imm_t3,
	[T32_32_SUB_i_T4] = exec_sub_imm_t4,
	[T32_32_CMP_i_T2] = exec_cmp_imm_t2,
	[T32_32_AND_i_T1] = exec_and_imm_t1,
	[T32_32_ANDS_i_T1] = exec_ands_imm_t1,
	[T32_32_MOV_i_T2] = exec_mov_imm_t2,
	[T32_32_MOVS_i_T2] = exec_movs_imm_t2,
	[T32_32_MOV_i_T3] = exec_mov_imm_t3,
	[T32_32_ADD_r_T3] = exec_add_reg_t3,
	[T32_32_ADDS_r_T3] = exec_adds_reg_t3,
	[T32_32_ADD_r_T3_RRX] = exec_add_reg_t3_rrx,
	[T32_32_ADDS_r_T3_RRX] = exec_adds_reg_t3_rrx,
	[T32_32_CMP_r_T3] = exec_cmp_reg_t3,
	[T32_32_CMP_r_T3_RRX] = exec_cmp_reg_t3_rrx,
	[T32_32_AND_r_T2] = exec_and_reg_t2,
	[T32_32_ANDS_r_T2] = exec_ands_reg_t2,
	[T32_32_AND_r_T2_RRX] = exec_and_reg_t2_rrx,
	[T32_32_ANDS_r_T2_RRX] = exec_ands_reg_t2_rrx,
	[T32_32_MOV_r_T3] = exec_mov_reg_t3,
	[T32_32_MOVS_r_T3] = exec_movs_reg_t3,
	[T32_32_MOV_r_T3_RRX] = exec_mov_reg_t3_rrx,
	[T32_32_MOVS_r_T3_RRX] = exec_movs_reg_t3_rrx,
	[T32_32_MOV_rr_T2] = exec_mov_rsr_t2,
	[T32_32_MOVS_rr_T2] = exec_movs_rsr_t2,
	[T32_32_TST_i_T1] = exec_tst_imm_t1,
	[T32_32_TEQ_i_T1] = exec_teq_imm_t1,
	[T32_32_CMN_i_T1] = exec_cmn_imm_t1,
	[T32_32_BIC_i_T1] = exec_bic_imm_t1,
	[T32_32_BICS_i_T1] = exec_bics_imm_t1,
	[T32_32_ORR_i_T1] = exec_orr_imm_t1,
	[T32_32_ORRS_i_T1] = exec_orrs_imm_t1,
	[T32_32_ORN_i_T1] = exec_orn_imm_t1,
	[T32_32_ORNS_i_T1] = exec_orns_imm_t1,
	[T32_32_EOR_i_T1] = exec_eor_imm_t1,
	[T32_32_EORS_i_T1] = exec_eors_imm_t1,
	[T32_32_MVN_i_T1] = exec_mvn_imm_t1,
	[T32_32_MVNS_i_T1] = exec_mvns_imm_t1,
	[T32_32_ADC_i_T1] = exec_adc_imm_t1,
	[T32_32_ADCS_i_T1] = exec_adcs_imm_t1,
	[T32_32_SBC_i_T1] = exec_sbc_imm_t1,
	[T32_32_SBCS_i_T1] = exec_sbcs_imm_t1,
	[T32_32_RSB_i_T2] = exec_rsb_imm_t2,
	[T32_32_RSBS_i_T2] = exec_rsbs_imm_t2,
	[T32_32_TST_r_T2] = exec_tst_reg_t2,
	[T32_32_TST_r_T2_RRX] = exec_tst_reg_t2_rrx,
	[T32_32_TEQ_r_T1] = exec_teq_reg_t1,
	[T32_32_TEQ_r_T1_RRX] = exec_teq_reg_t1_rrx,
	[T32_32_CMN_r_T2] = exec_cmn_reg_t2,
	[T32_32_CMN_r_T2_RRX] = exec_cmn_reg_t2_rrx,
	[T32_32_BIC_r_T2] = exec_bic_reg_t2,
	[T32_32_BIC_r_T2_RRX] = exec_bic_reg_t2_rrx,
	[T32_32_BICS_r_T2] = exec_bics_reg_t2,
	[T32_32_BICS_r_T2_RRX] = exec_bics_reg_t2_rrx,
	[T32_32_ORR_r_T2] = exec_orr_reg_t2,
	[T32_32_ORR_r_T2_RRX] = exec_orr_reg_t2_rrx,
	[T32_32_ORRS_r_T2] = exec_orrs_reg_t2,
	[T32_32_ORRS_r_T2_RRX] = exec_orrs_reg_t2_rrx,
	[T32_32_ORN_r_T1] = exec_orn_reg_t1,
	[T32_32_ORN_r_T1_RRX] = exec_orn_reg_t1_rrx,
	[T32_32_ORNS_r_T1] = exec_orns_reg_t1,
	[T32_32_ORNS_r_T1_RRX] = exec_orns_reg_t1_rrx,
	[T32_32_EOR_r_T2] = exec_eor_reg_t2,
	[T32_32_EOR_r_T2_RRX] = exec_eor_reg_t2_rrx,
	[T32_32_EORS_r_T2] = exec_eors_reg_t2,
	[T32_32_EORS_r_T2_RRX] = exec_eors_reg_t2_rrx,
	[T32_32_MVN_r_T2] = exec_mvn_reg_t2,
	[T32_32_MVN_r_T2_RRX] = exec_mvn_reg_t2_rrx,
	[T32_32_MVNS_r_T2] = exec_mvns_reg_t2,
	[T32_32_MVNS_r_T2_RRX] = exec_mvns_reg_t2_rrx,
	[T32_32_ADC_r_T2] = exec_adc_reg_t2,
	[T32_32_ADC_r_T2_RRX] = exec_adc_reg_t2_rrx,
	[T32_32_ADCS_r_T2] = exec_adcs_reg_t2,
	[T32_32_ADCS_r_T2_RRX] = exec_adcs_reg_t2_rrx,
	[T32_32_SBC_r_T2] = exec_sbc_reg_t2,
	[T32_32_SBC_r_T2_RRX] = exec_sbc_reg_t2_rrx,
	[T32_32_SBCS_r_T2] = exec_sbcs_reg_t2,
	[T32_32_SBCS_r_T2_RRX] = exec_sbcs_reg_t2_rrx,
	[T32_32_SUB_r_T2] = exec_sub_reg_t2,
	[T32_32_SUB_r_T2_RRX] = exec_sub_reg_t2_rrx,
	[T32_32_SUBS_r_T2] = exec_subs_reg_t2,
	[T32_32_SUBS_r_T2_RRX] = exec_subs_reg_t2_rrx,
	[T32_32_RSB_r_T1] = exec_rsb_reg_t1,
	[T32_32_RSB_r_T1_RRX] = exec_rsb_reg_t1_rrx,
	[T32_32_RSBS_r_T1] = exec_rsbs_reg_t1,
	[T32_32_RSBS_r_T1_RRX] = exec_rsbs_reg_t1_rrx,
	[T32_32_B_T3] = exec_b_t3,
	[T32_32_B_T4] = exec_b_t4,
};

Std_ReturnType thumb_execute32(TargetCoreType *core, uint32 insn)
{
	T32Decode32Id id = t32_decode32(insn);

	if (executors[id] != NULL) {
		return executors[id](core, insn);
	}
	if (thumb_gen_executors32[id] != NULL) {
		return thumb_gen_executors32[id](core, insn);
	}
	return STD_E_DECODE;
}

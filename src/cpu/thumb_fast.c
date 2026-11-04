/*
 * Fast executors of the most frequent Thumb encodings (see thumb_fast.h).
 *
 * Each one does what its generic executor (thumb_exec16.c, thumb_exec32.c,
 * thumb_gen16.c, thumb_gen32.c) and the pseudo-code it calls do, in the same
 * order (the UNPREDICTABLE checks of the encoding, the condition, the memory
 * access before the base register's write-back), with the same profile
 * functions for registers, memory and PC writes, but without filling the
 * pseudo-code's input and output structures. test/cpu/test_thumb_fast.c runs
 * both on random encodings and states and compares the results.
 */
#include "thumb_fast.h"
#include "thumb_glue.h"
#include "t32_decode16.h"
#include "t32_decode32.h"
#include "arm_common/arm_pseudo_branch.h"
#include "memory_window.h"
#include <string.h>

/*
 * ConditionHolds(cond, APSR) as a table: bit NZCV of cond_table[cond], made
 * from ConditionHolds itself (thumb_fast_init).
 */
static uint16 cond_table[16];

static void thumb_fast_init(void)
{
	uint32 cond;
	uint32 nzcv;

	if (cond_table[ARM_COND_AL] != 0U) {
		return;
	}
	for (cond = 0U; cond < 16U; cond++) {
		for (nzcv = 0U; nzcv < 16U; nzcv++) {
			if (ConditionHolds(cond, nzcv << 28)) {
				cond_table[cond] |= (uint16)(1U << nzcv);
			}
		}
	}
}

static inline bool holds(uint32 cond, uint32 apsr)
{
	return ((cond_table[cond & 0xFU] >> (apsr >> 28)) & 1U) != 0U;
}

/* ConditionPassed() of the current instruction (the IT condition, or AL). */
static inline bool passed(const TargetCoreType *core)
{
	return (core->reg.itstate == 0U) || holds(Stm32CurrentCond(core), ArmAPSR(core));
}

/* APSR.N, Z, C, V of AddWithCarry(x, y, carry_in). */
static inline void flags_add(TargetCoreType *core, uint32 x, uint32 y, bool carry_in)
{
	PseudoCodeStatusFlagType flags = ApsrFlags(ArmAPSR(core));
	uint32 result = AddWithCarry(x, y, carry_in, &flags);

	ArmAPSRWrite(core, ApsrUpdateNZCV(ArmAPSR(core), result, &flags));
}

/* R[d] = AddWithCarry(x, y, carry_in), with the flags if setflags (d is not PC). */
static inline void write_add(TargetCoreType *core, uint32 d, uint32 x, uint32 y, bool carry_in, bool setflags)
{
	PseudoCodeStatusFlagType flags = ApsrFlags(ArmAPSR(core));
	uint32 result = AddWithCarry(x, y, carry_in, &flags);

	ArmRWrite(core, d, result);
	if (setflags) {
		ArmAPSRWrite(core, ApsrUpdateNZCV(ArmAPSR(core), result, &flags));
	}
}

/* R[t] = MemU[address, size] for a destination that is not PC. */
static inline Std_ReturnType load(TargetCoreType *core, uint32 t, uint32 address, uint32 size)
{
	uint32 data;
	Std_ReturnType err = ArmMemU_R(core, address, size, &data);

	if (err != STD_E_OK) {
		return err;
	}
	ArmRWrite(core, t, data);
	return STD_E_OK;
}

/* A word loaded into R[t] (ArmLoadWordWrite): PC takes LoadWritePC. */
static inline Std_ReturnType load_word(TargetCoreType *core, uint32 t, uint32 address)
{
	uint32 data;
	Std_ReturnType err = ArmMemU_R(core, address, 4U, &data);

	if (err != STD_E_OK) {
		return err;
	}
	if (t == ARM_REG_PC) {
		if ((address & 0x3U) != 0U) {
			return STD_E_INVALID;
		}
		return ArmLoadWritePC(core, data);
	}
	ArmRWrite(core, t, data);
	return STD_E_OK;
}

/* MemU[R[n]] = R[t]; R[n] = R[n] +/- imm32 (post-indexed). */
static inline Std_ReturnType store_post(TargetCoreType *core, uint32 t, uint32 n, uint32 size, uint32 imm32, bool add)
{
	uint32 rn = ArmReadReg(core, n);
	Std_ReturnType err = ArmMemU_W(core, rn, size, ArmReadReg(core, t));

	if (err != STD_E_OK) {
		return err;
	}
	ArmRWrite(core, n, add ? (rn + imm32) : (rn - imm32));
	return STD_E_OK;
}

/*
 * 16-bit encodings
 */

/* B T1: B<c> <label>; InITBlock: UNPREDICTABLE. */
static Std_ReturnType fast_b_t1(TargetCoreType *core, uint16 insn)
{
	if (Stm32InITBlock(core)) {
		return STD_E_INVALID;
	}
	if (holds(t32_B_T1_cond(insn), ArmAPSR(core))) {
		return ArmBranchWritePC(core, ArmPC(core) + SignExtend(t32_B_T1_imm8(insn) << 1, 9U));
	}
	return STD_E_OK;
}

/* B T2: B <label>; InITBlock && !LastInITBlock: UNPREDICTABLE. */
static Std_ReturnType fast_b_t2(TargetCoreType *core, uint16 insn)
{
	if (thumb_in_it_not_last(core)) {
		return STD_E_INVALID;
	}
	if (passed(core)) {
		return ArmBranchWritePC(core, ArmPC(core) + SignExtend(t32_B_T2_imm11(insn) << 1, 12U));
	}
	return STD_E_OK;
}

/* CMP (register) T1: CMP <Rn>, <Rm>. */
static Std_ReturnType fast_cmp_r_t1(TargetCoreType *core, uint16 insn)
{
	if (passed(core)) {
		flags_add(core, ArmR(core, t32_CMP_r_T1_Rn(insn)), ~ArmR(core, t32_CMP_r_T1_Rm(insn)), TRUE);
	}
	return STD_E_OK;
}

/* CMP (immediate) T1: CMP <Rn>, #<imm8>. */
static Std_ReturnType fast_cmp_i_t1(TargetCoreType *core, uint16 insn)
{
	if (passed(core)) {
		flags_add(core, ArmR(core, t32_CMP_i_T1_Rn(insn)), ~(uint32)t32_CMP_i_T1_imm8(insn), TRUE);
	}
	return STD_E_OK;
}

/* MOV (register) T1: MOV <Rd>, <Rm> (high registers too; no flags; d == 15 is ALUWritePC). */
static Std_ReturnType fast_mov_r_t1(TargetCoreType *core, uint16 insn)
{
	uint32 d = (t32_MOV_r_T1_D(insn) << 3) | t32_MOV_r_T1_Rd(insn);
	uint32 value;

	if ((d == 15U) && thumb_in_it_not_last(core)) {
		return STD_E_INVALID;
	}
	if (!passed(core)) {
		return STD_E_OK;
	}
	value = ArmReadReg(core, t32_MOV_r_T1_Rm(insn));
	if (d == ARM_REG_PC) {
		return ArmALUWritePC(core, value);
	}
	ArmRWrite(core, d, value);
	return STD_E_OK;
}

/* MOV (immediate) T1: MOVS <Rd>, #<imm8> (flags N, Z outside an IT block; C, V kept). */
static Std_ReturnType fast_mov_i_t1(TargetCoreType *core, uint16 insn)
{
	uint32 result = t32_MOV_i_T1_imm8(insn);
	PseudoCodeStatusFlagType flags;

	if (!passed(core)) {
		return STD_E_OK;
	}
	ArmRWrite(core, t32_MOV_i_T1_Rd(insn), result);
	if (!Stm32InITBlock(core)) {
		flags = ApsrFlags(ArmAPSR(core));
		ArmAPSRWrite(core, ApsrUpdateNZCV(ArmAPSR(core), result, &flags));
	}
	return STD_E_OK;
}

/* ADD (immediate) T2: ADDS <Rdn>, #<imm8>. */
static Std_ReturnType fast_add_i_t2(TargetCoreType *core, uint16 insn)
{
	uint32 dn = t32_ADD_i_T2_Rdn(insn);

	if (passed(core)) {
		write_add(core, dn, ArmR(core, dn), t32_ADD_i_T2_imm8(insn), FALSE, !Stm32InITBlock(core));
	}
	return STD_E_OK;
}

/* SUB (immediate) T2: SUBS <Rdn>, #<imm8>. */
static Std_ReturnType fast_sub_i_t2(TargetCoreType *core, uint16 insn)
{
	uint32 dn = t32_SUB_i_T2_Rdn(insn);

	if (passed(core)) {
		write_add(core, dn, ArmR(core, dn), ~(uint32)t32_SUB_i_T2_imm8(insn), TRUE, !Stm32InITBlock(core));
	}
	return STD_E_OK;
}

/* ADD (SP plus immediate) T1: ADD <Rd>, SP, #<imm8:'00'>. */
static Std_ReturnType fast_add_sp_i_t1(TargetCoreType *core, uint16 insn)
{
	if (passed(core)) {
		write_add(core, t32_ADD_SP_i_T1_Rd(insn), ArmR(core, 13U), t32_ADD_SP_i_T1_imm8(insn) << 2, FALSE, FALSE);
	}
	return STD_E_OK;
}

/* ADD (SP plus immediate) T2: ADD SP, SP, #<imm7:'00'>. */
static Std_ReturnType fast_add_sp_i_t2(TargetCoreType *core, uint16 insn)
{
	if (passed(core)) {
		write_add(core, 13U, ArmR(core, 13U), t32_ADD_SP_i_T2_imm7(insn) << 2, FALSE, FALSE);
	}
	return STD_E_OK;
}

/* SUB (SP minus immediate) T1: SUB SP, SP, #<imm7:'00'>. */
static Std_ReturnType fast_sub_sp_i_t1(TargetCoreType *core, uint16 insn)
{
	if (passed(core)) {
		write_add(core, 13U, ArmR(core, 13U), ~(t32_SUB_SP_i_T1_imm7(insn) << 2), TRUE, FALSE);
	}
	return STD_E_OK;
}

/* LDR (immediate) T1: LDR <Rt>, [<Rn>, #<imm5:'00'>]. */
static Std_ReturnType fast_ldr_i_t1(TargetCoreType *core, uint16 insn)
{
	if (!passed(core)) {
		return STD_E_OK;
	}
	return load(core, t32_LDR_i_T1_Rt(insn), ArmR(core, t32_LDR_i_T1_Rn(insn)) + (t32_LDR_i_T1_imm5(insn) << 2), 4U);
}

/* LDR (immediate) T2: LDR <Rt>, [SP, #<imm8:'00'>]. */
static Std_ReturnType fast_ldr_i_t2(TargetCoreType *core, uint16 insn)
{
	if (!passed(core)) {
		return STD_E_OK;
	}
	return load(core, t32_LDR_i_T2_Rt(insn), ArmR(core, 13U) + (t32_LDR_i_T2_imm8(insn) << 2), 4U);
}

/* LDRB (immediate) T1: LDRB <Rt>, [<Rn>, #<imm5>]. */
static Std_ReturnType fast_ldrb_i_t1(TargetCoreType *core, uint16 insn)
{
	if (!passed(core)) {
		return STD_E_OK;
	}
	return load(core, t32_LDRB_i_T1_Rt(insn), ArmR(core, t32_LDRB_i_T1_Rn(insn)) + t32_LDRB_i_T1_imm5(insn), 1U);
}

/* LDR (literal) T1: LDR <Rt>, [PC, #<imm8:'00'>] from Align(PC, 4). */
static Std_ReturnType fast_ldr_l_t1(TargetCoreType *core, uint16 insn)
{
	if (!passed(core)) {
		return STD_E_OK;
	}
	return load(core, t32_LDR_l_T1_Rt(insn), Align(ArmPC(core), 4U) + (t32_LDR_l_T1_imm8(insn) << 2), 4U);
}

/* STR (immediate) T1: STR <Rt>, [<Rn>, #<imm5:'00'>]. */
static Std_ReturnType fast_str_i_t1(TargetCoreType *core, uint16 insn)
{
	if (!passed(core)) {
		return STD_E_OK;
	}
	return ArmMemU_W(core, ArmR(core, t32_STR_i_T1_Rn(insn)) + (t32_STR_i_T1_imm5(insn) << 2), 4U,
		ArmR(core, t32_STR_i_T1_Rt(insn)));
}

/* STR (immediate) T2: STR <Rt>, [SP, #<imm8:'00'>]. */
static Std_ReturnType fast_str_i_t2(TargetCoreType *core, uint16 insn)
{
	if (!passed(core)) {
		return STD_E_OK;
	}
	return ArmMemU_W(core, ArmR(core, 13U) + (t32_STR_i_T2_imm8(insn) << 2), 4U, ArmR(core, t32_STR_i_T2_Rt(insn)));
}

/* CBZ, CBNZ T1: no condition; InITBlock: UNPREDICTABLE. */
static inline Std_ReturnType compare_branch(TargetCoreType *core, uint32 n, uint32 imm32, bool nonzero)
{
	if (Stm32InITBlock(core)) {
		return STD_E_INVALID;
	}
	if (nonzero != (ArmR(core, n) == 0U)) {
		return ArmBranchWritePC(core, ArmPC(core) + imm32);
	}
	return STD_E_OK;
}

static Std_ReturnType fast_cbz_t1(TargetCoreType *core, uint16 insn)
{
	return compare_branch(core, t32_CBZ_T1_Rn(insn), (t32_CBZ_T1_i(insn) << 6) | (t32_CBZ_T1_imm5(insn) << 1), FALSE);
}

static Std_ReturnType fast_cbnz_t1(TargetCoreType *core, uint16 insn)
{
	return compare_branch(core, t32_CBNZ_T1_Rn(insn), (t32_CBNZ_T1_i(insn) << 6) | (t32_CBNZ_T1_imm5(insn) << 1), TRUE);
}

/* BX T1: BX <Rm>; InITBlock && !LastInITBlock: UNPREDICTABLE. */
static Std_ReturnType fast_bx_t1(TargetCoreType *core, uint16 insn)
{
	if (thumb_in_it_not_last(core)) {
		return STD_E_INVALID;
	}
	if (passed(core)) {
		return ArmBXWritePC(core, ArmReadReg(core, t32_BX_T1_Rm(insn)));
	}
	return STD_E_OK;
}

/* UXTB T1: UXTB <Rd>, <Rm>. */
static Std_ReturnType fast_uxtb_t1(TargetCoreType *core, uint16 insn)
{
	if (passed(core)) {
		ArmRWrite(core, t32_UXTB_T1_Rd(insn), ArmR(core, t32_UXTB_T1_Rm(insn)) & 0xFFU);
	}
	return STD_E_OK;
}

/*
 * PUSH T1 (STMDB SP!) and POP T1 (LDM SP!): when the whole transfer is
 * aligned plain memory (no fault can stop it half-way), the words are moved
 * directly; anything else is left to the generic executor.
 */
static Std_ReturnType fast_push_t1(TargetCoreType *core, uint16 insn)
{
	uint32 registers = (t32_PUSH_T1_M(insn) << 14) | (insn & 0xFFU);
	uint32 size = 4U * BitCount(registers);
	uint32 address;
	uint8 *p;
	uint32 i;

	if ((registers == 0U) || !passed(core)) {
		return thumb_executor16(insn)(core, insn);
	}
	address = ArmR(core, 13U) - size;
	p = ((address & 3U) == 0U) ? memory_window_write_pointer(address, size) : NULL;
	if (p == NULL) {
		return thumb_executor16(insn)(core, insn);
	}
	for (i = 0U; i < 15U; i++) {
		if (ARM_BIT(registers, i) != 0U) {
			uint32 v = ArmR(core, i);

			memcpy(p, &v, 4U);
			p += 4U;
		}
	}
	ArmRWrite(core, 13U, address);
	return STD_E_OK;
}

static Std_ReturnType fast_pop_t1(TargetCoreType *core, uint16 insn)
{
	uint32 registers = (t32_POP_T1_P(insn) << 15) | (insn & 0xFFU);
	uint32 size = 4U * BitCount(registers);
	uint32 address = ArmR(core, 13U);
	const uint8 *p;
	uint32 pc = 0U;
	uint32 i;

	if ((registers == 0U) || ((ARM_BIT(registers, 15U) != 0U) && thumb_in_it_not_last(core)) || !passed(core)) {
		return thumb_executor16(insn)(core, insn);
	}
	p = ((address & 3U) == 0U) ? memory_window_pointer(address, size) : NULL;
	if (p == NULL) {
		return thumb_executor16(insn)(core, insn);
	}
	for (i = 0U; i < 16U; i++) {
		if (ARM_BIT(registers, i) != 0U) {
			uint32 v;

			memcpy(&v, p, 4U);
			p += 4U;
			if (i == 15U) {
				pc = v;
			}
			else {
				ArmRWrite(core, i, v);
			}
		}
	}
	ArmRWrite(core, 13U, address + size);
	if (ARM_BIT(registers, 15U) != 0U) {
		return ArmLoadWritePC(core, pc);
	}
	return STD_E_OK;
}

/*
 * 32-bit encodings
 */

/* BL T1: BL <label>; InITBlock && !LastInITBlock: UNPREDICTABLE. */
static Std_ReturnType fast_bl_i_t1(TargetCoreType *core, uint32 insn)
{
	uint32 s = t32w_BL_i_T1_S(insn);
	uint32 i1 = (t32w_BL_i_T1_J1(insn) ^ s) ^ 1U;
	uint32 i2 = (t32w_BL_i_T1_J2(insn) ^ s) ^ 1U;
	uint32 imm32 = SignExtend((s << 24) | (i1 << 23) | (i2 << 22) | (t32w_BL_i_T1_imm10(insn) << 12)
		| (t32w_BL_i_T1_imm11(insn) << 1), 25U);

	if (thumb_in_it_not_last(core)) {
		return STD_E_INVALID;
	}
	if (!passed(core)) {
		return STD_E_OK;
	}
	ArmRWrite(core, ARM_REG_LR, ArmPC(core) | 1U);
	return ArmBranchWritePC(core, ArmPC(core) + imm32);
}

/* B T3: B<c>.W <label>; InITBlock: UNPREDICTABLE. */
static Std_ReturnType fast_b_t3(TargetCoreType *core, uint32 insn)
{
	uint32 imm21 = (t32w_B_T3_S(insn) << 20) | (t32w_B_T3_J2(insn) << 19) | (t32w_B_T3_J1(insn) << 18)
		| (t32w_B_T3_imm6(insn) << 12) | (t32w_B_T3_imm11(insn) << 1);

	if (Stm32InITBlock(core)) {
		return STD_E_INVALID;
	}
	if (holds(t32w_B_T3_cond(insn), ArmAPSR(core))) {
		return ArmBranchWritePC(core, ArmPC(core) + SignExtend(imm21, 21U));
	}
	return STD_E_OK;
}

/* ADD (immediate) T3: ADD.W <Rd>, <Rn>, #<const>; d IN {13, 15} or n == 15: UNPREDICTABLE. */
static Std_ReturnType fast_add_i_t3(TargetCoreType *core, uint32 insn)
{
	uint32 d = t32w_ADD_i_T3_Rd(insn);
	uint32 n = t32w_ADD_i_T3_Rn(insn);

	if ((d == 13U) || (d == 15U) || (n == 15U)) {
		return STD_E_INVALID;
	}
	if (passed(core)) {
		write_add(core, d, ArmR(core, n),
			ThumbExpandImm(thumb_imm12(t32w_ADD_i_T3_i(insn), t32w_ADD_i_T3_imm3(insn), t32w_ADD_i_T3_imm8(insn))),
			FALSE, FALSE);
	}
	return STD_E_OK;
}

/* LDR (immediate) T3: LDR.W <Rt>, [<Rn>, #<imm12>]; t == 15 inside an IT block but last: UNPREDICTABLE. */
static Std_ReturnType fast_ldr_i_t3(TargetCoreType *core, uint32 insn)
{
	uint32 t = t32w_LDR_i_T3_Rt(insn);

	if ((t == 15U) && thumb_in_it_not_last(core)) {
		return STD_E_INVALID;
	}
	if (!passed(core)) {
		return STD_E_OK;
	}
	return load_word(core, t, ArmReadReg(core, t32w_LDR_i_T3_Rn(insn)) + t32w_LDR_i_T3_imm12(insn));
}

/* LDRB (immediate) T2: LDRB.W <Rt>, [<Rn>, #<imm12>]; t == 13: UNPREDICTABLE. */
static Std_ReturnType fast_ldrb_i_t2(TargetCoreType *core, uint32 insn)
{
	uint32 t = t32w_LDRB_i_T2_Rt(insn);

	if (t == 13U) {
		return STD_E_INVALID;
	}
	if (!passed(core)) {
		return STD_E_OK;
	}
	return load(core, t, ArmReadReg(core, t32w_LDRB_i_T2_Rn(insn)) + t32w_LDRB_i_T2_imm12(insn), 1U);
}

/* STRB (immediate) T3, post-indexed: STRB <Rt>, [<Rn>], #+/-<imm8>; t IN {13, 15} or n == t: UNPREDICTABLE. */
static Std_ReturnType fast_strb_i_t3_post(TargetCoreType *core, uint32 insn)
{
	uint32 t = t32w_STRB_i_T3_post_Rt(insn);
	uint32 n = t32w_STRB_i_T3_post_Rn(insn);

	if (thumb_sp_or_pc(t) || (n == t)) {
		return STD_E_INVALID;
	}
	if (!passed(core)) {
		return STD_E_OK;
	}
	return store_post(core, t, n, 1U, t32w_STRB_i_T3_post_imm8(insn), t32w_STRB_i_T3_post_U(insn) != 0U);
}

/* STR (immediate) T4, post-indexed: STR <Rt>, [<Rn>], #+/-<imm8>; t == 15 or n == t: UNPREDICTABLE. */
static Std_ReturnType fast_str_i_t4_post(TargetCoreType *core, uint32 insn)
{
	uint32 t = t32w_STR_i_T4_post_Rt(insn);
	uint32 n = t32w_STR_i_T4_post_Rn(insn);

	if ((t == 15U) || (n == t)) {
		return STD_E_INVALID;
	}
	if (!passed(core)) {
		return STD_E_OK;
	}
	return store_post(core, t, n, 4U, t32w_STR_i_T4_post_imm8(insn), t32w_STR_i_T4_post_U(insn) != 0U);
}

ThumbExec16 thumb_fast_executor16(uint16 insn)
{
	thumb_fast_init();
	switch (t32_decode16(insn)) {
	case T32_16_PUSH_T1: return fast_push_t1;
	case T32_16_POP_T1: return fast_pop_t1;
	case T32_16_B_T1: return fast_b_t1;
	case T32_16_B_T2: return fast_b_t2;
	case T32_16_CMP_r_T1: return fast_cmp_r_t1;
	case T32_16_CMP_i_T1: return fast_cmp_i_t1;
	case T32_16_MOV_r_T1: return fast_mov_r_t1;
	case T32_16_MOV_i_T1: return fast_mov_i_t1;
	case T32_16_ADD_i_T2: return fast_add_i_t2;
	case T32_16_SUB_i_T2: return fast_sub_i_t2;
	case T32_16_ADD_SP_i_T1: return fast_add_sp_i_t1;
	case T32_16_ADD_SP_i_T2: return fast_add_sp_i_t2;
	case T32_16_SUB_SP_i_T1: return fast_sub_sp_i_t1;
	case T32_16_LDR_i_T1: return fast_ldr_i_t1;
	case T32_16_LDR_i_T2: return fast_ldr_i_t2;
	case T32_16_LDRB_i_T1: return fast_ldrb_i_t1;
	case T32_16_LDR_l_T1: return fast_ldr_l_t1;
	case T32_16_STR_i_T1: return fast_str_i_t1;
	case T32_16_STR_i_T2: return fast_str_i_t2;
	case T32_16_CBZ_T1: return fast_cbz_t1;
	case T32_16_CBNZ_T1: return fast_cbnz_t1;
	case T32_16_BX_T1: return fast_bx_t1;
	case T32_16_UXTB_T1: return fast_uxtb_t1;
	default: return NULL;
	}
}

ThumbExec32 thumb_fast_executor32(uint32 insn)
{
	thumb_fast_init();
	switch (t32_decode32(insn)) {
	case T32_32_BL_i_T1: return fast_bl_i_t1;
	case T32_32_B_T3: return fast_b_t3;
	case T32_32_ADD_i_T3: return fast_add_i_t3;
	case T32_32_LDR_i_T3: return fast_ldr_i_t3;
	case T32_32_LDRB_i_T2: return fast_ldrb_i_t2;
	case T32_32_STRB_i_T3_post: return fast_strb_i_t3_post;
	case T32_32_STR_i_T4_post: return fast_str_i_t4_post;
	default: return NULL;
	}
}

/*
 * 16-bit Thumb instructions: decoded with the table generated from Arm's
 * machine-readable data (decoder/t32_decode16.h), executed here. Semantics:
 * Arm Architecture Reference Manual, Armv7-M. Instructions without an
 * executor are reported as unsupported.
 */
#include "cpu.h"
#include "bus.h"
#include "std_errno.h"
#include "t32_decode16.h"

typedef Std_ReturnType (*ThumbExec16)(TargetCoreType *core, uint16 insn, uint32 *next);

static void set_nz(TargetCoreType *core, uint32 result)
{
	core->reg.apsr &= ~(APSR_N | APSR_Z);
	if ((result & 0x80000000U) != 0U) {
		core->reg.apsr |= APSR_N;
	}
	if (result == 0U) {
		core->reg.apsr |= APSR_Z;
	}
}

static void set_c(TargetCoreType *core, uint32 carry)
{
	core->reg.apsr &= ~APSR_C;
	if (carry != 0U) {
		core->reg.apsr |= APSR_C;
	}
}

/* AddWithCarry() of the architecture, setting N, Z, C and V. */
static uint32 add_with_carry(TargetCoreType *core, uint32 x, uint32 y, uint32 carry_in)
{
	uint64 unsigned_sum = (uint64)x + (uint64)y + carry_in;
	sint64 signed_sum = (sint64)(sint32)x + (sint64)(sint32)y + (sint64)carry_in;
	uint32 result = (uint32)unsigned_sum;

	set_nz(core, result);
	set_c(core, (uint64)result != unsigned_sum);
	core->reg.apsr &= ~APSR_V;
	if ((sint64)(sint32)result != signed_sum) {
		core->reg.apsr |= APSR_V;
	}
	return result;
}

/* ConditionPassed() for the 4-bit condition field. */
static std_bool condition_passed(const TargetCoreType *core, uint32 cond)
{
	uint32 apsr = core->reg.apsr;
	std_bool n = (apsr & APSR_N) != 0U;
	std_bool z = (apsr & APSR_Z) != 0U;
	std_bool c = (apsr & APSR_C) != 0U;
	std_bool v = (apsr & APSR_V) != 0U;
	std_bool result;

	switch (cond >> 1) {
	case 0: result = z; break;
	case 1: result = c; break;
	case 2: result = n; break;
	case 3: result = v; break;
	case 4: result = c && !z; break;
	case 5: result = (n == v); break;
	case 6: result = (n == v) && !z; break;
	default: result = TRUE; break;
	}
	if (((cond & 1U) != 0U) && (cond != 0xFU)) {
		result = !result;
	}
	return result;
}

static Std_ReturnType load(TargetCoreType *core, uint32 address, uint32 size, uint32 *value)
{
	uint8 v8;
	Std_ReturnType err;

	if (size == 1U) {
		err = bus_get_data8(core->core_id, address, &v8);
		*value = v8;
		return err;
	}
	return bus_get_data32(core->core_id, address, value);
}

static Std_ReturnType store(TargetCoreType *core, uint32 address, uint32 size, uint32 value)
{
	if (size == 1U) {
		return bus_put_data8(core->core_id, address, (uint8)value);
	}
	return bus_put_data32(core->core_id, address, value);
}

/* Align(PC, 4) as seen by the instruction: its address + 4, word-aligned. */
static uint32 aligned_pc(const TargetCoreType *core)
{
	return (core->reg.pc + 4U) & ~3U;
}

static Std_ReturnType exec_ldr_literal(TargetCoreType *core, uint16 insn, uint32 *next)
{
	(void)next;
	return load(core, aligned_pc(core) + (t32_LDR_l_T1_imm8(insn) << 2), 4U,
		&core->reg.r[t32_LDR_l_T1_Rt(insn)]);
}

static Std_ReturnType exec_adr(TargetCoreType *core, uint16 insn, uint32 *next)
{
	(void)next;
	core->reg.r[t32_ADR_T1_Rd(insn)] = aligned_pc(core) + (t32_ADR_T1_imm8(insn) << 2);
	return STD_E_OK;
}

static Std_ReturnType exec_ldr_imm(TargetCoreType *core, uint16 insn, uint32 *next)
{
	(void)next;
	return load(core, core->reg.r[t32_LDR_i_T1_Rn(insn)] + (t32_LDR_i_T1_imm5(insn) << 2), 4U,
		&core->reg.r[t32_LDR_i_T1_Rt(insn)]);
}

static Std_ReturnType exec_str_imm(TargetCoreType *core, uint16 insn, uint32 *next)
{
	(void)next;
	return store(core, core->reg.r[t32_STR_i_T1_Rn(insn)] + (t32_STR_i_T1_imm5(insn) << 2), 4U,
		core->reg.r[t32_STR_i_T1_Rt(insn)]);
}

static Std_ReturnType exec_ldrb_imm(TargetCoreType *core, uint16 insn, uint32 *next)
{
	(void)next;
	return load(core, core->reg.r[t32_LDRB_i_T1_Rn(insn)] + t32_LDRB_i_T1_imm5(insn), 1U,
		&core->reg.r[t32_LDRB_i_T1_Rt(insn)]);
}

static Std_ReturnType exec_strb_imm(TargetCoreType *core, uint16 insn, uint32 *next)
{
	(void)next;
	return store(core, core->reg.r[t32_STRB_i_T1_Rn(insn)] + t32_STRB_i_T1_imm5(insn), 1U,
		core->reg.r[t32_STRB_i_T1_Rt(insn)]);
}

/* MOVS Rd, #imm8: C is unchanged. */
static Std_ReturnType exec_movs_imm(TargetCoreType *core, uint16 insn, uint32 *next)
{
	uint32 value = t32_MOV_i_T1_imm8(insn);

	(void)next;
	core->reg.r[t32_MOV_i_T1_Rd(insn)] = value;
	set_nz(core, value);
	return STD_E_OK;
}

static Std_ReturnType exec_cmp_imm(TargetCoreType *core, uint16 insn, uint32 *next)
{
	(void)next;
	(void)add_with_carry(core, core->reg.r[t32_CMP_i_T1_Rn(insn)], ~t32_CMP_i_T1_imm8(insn), 1U);
	return STD_E_OK;
}

static Std_ReturnType exec_adds_imm8(TargetCoreType *core, uint16 insn, uint32 *next)
{
	uint32 rdn = t32_ADD_i_T2_Rdn(insn);

	(void)next;
	core->reg.r[rdn] = add_with_carry(core, core->reg.r[rdn], t32_ADD_i_T2_imm8(insn), 0U);
	return STD_E_OK;
}

static Std_ReturnType exec_subs_imm8(TargetCoreType *core, uint16 insn, uint32 *next)
{
	uint32 rdn = t32_SUB_i_T2_Rdn(insn);

	(void)next;
	core->reg.r[rdn] = add_with_carry(core, core->reg.r[rdn], ~t32_SUB_i_T2_imm8(insn), 1U);
	return STD_E_OK;
}

/*
 * MOVS Rd, Rm, <shift> #imm (LSLS, LSRS, ASRS immediate; op 0b11 is not this
 * instruction). LSL #0 is a plain MOVS (C unchanged); LSR/ASR #0 mean #32.
 */
static Std_ReturnType exec_movs_shift_imm(TargetCoreType *core, uint16 insn, uint32 *next)
{
	uint32 op = t32_MOV_r_T2_op(insn);
	uint32 imm5 = t32_MOV_r_T2_imm5(insn);
	uint32 rm = core->reg.r[t32_MOV_r_T2_Rm(insn)];
	uint32 amount = ((op != 0U) && (imm5 == 0U)) ? 32U : imm5;
	uint32 result = rm;

	(void)next;
	if (amount != 0U) {
		if (op == 0U) {
			set_c(core, (rm >> (32U - amount)) & 1U);
			result = rm << amount;
		}
		else if (op == 1U) {
			set_c(core, (rm >> (amount - 1U)) & 1U);
			result = (amount == 32U) ? 0U : (rm >> amount);
		}
		else {
			set_c(core, (uint32)((sint32)rm >> (amount - 1U)) & 1U);
			result = (amount == 32U) ? (uint32)((sint32)rm >> 31) : (uint32)((sint32)rm >> amount);
		}
	}
	core->reg.r[t32_MOV_r_T2_Rd(insn)] = result;
	set_nz(core, result);
	return STD_E_OK;
}

static Std_ReturnType exec_b_cond(TargetCoreType *core, uint16 insn, uint32 *next)
{
	if (condition_passed(core, t32_B_T1_cond(insn))) {
		*next = core->reg.pc + 4U + (uint32)((sint32)(sint8)t32_B_T1_imm8(insn) << 1);
	}
	return STD_E_OK;
}

static Std_ReturnType exec_b(TargetCoreType *core, uint16 insn, uint32 *next)
{
	sint32 offset = (sint32)(t32_B_T2_imm11(insn) << 21) >> 20;

	*next = core->reg.pc + 4U + (uint32)offset;
	return STD_E_OK;
}

/* BX Rm: Thumb state only (bit 0 of the target selects it). */
static Std_ReturnType exec_bx(TargetCoreType *core, uint16 insn, uint32 *next)
{
	*next = core->reg.r[t32_BX_T1_Rm(insn)] & ~1U;
	return STD_E_OK;
}

static Std_ReturnType exec_nop(TargetCoreType *core, uint16 insn, uint32 *next)
{
	(void)core;
	(void)insn;
	(void)next;
	return STD_E_OK;
}

/* WFI: there are no interrupt sources yet, so the core stays halted. */
static Std_ReturnType exec_wfi(TargetCoreType *core, uint16 insn, uint32 *next)
{
	(void)insn;
	(void)next;
	core->is_halt = TRUE;
	return STD_E_OK;
}

static const ThumbExec16 executors[T32_16_ID_NUM] = {
	[T32_16_LDR_l_T1] = exec_ldr_literal,
	[T32_16_ADR_T1] = exec_adr,
	[T32_16_LDR_i_T1] = exec_ldr_imm,
	[T32_16_STR_i_T1] = exec_str_imm,
	[T32_16_LDRB_i_T1] = exec_ldrb_imm,
	[T32_16_STRB_i_T1] = exec_strb_imm,
	[T32_16_MOV_i_T1] = exec_movs_imm,
	[T32_16_CMP_i_T1] = exec_cmp_imm,
	[T32_16_ADD_i_T2] = exec_adds_imm8,
	[T32_16_SUB_i_T2] = exec_subs_imm8,
	[T32_16_MOV_r_T2] = exec_movs_shift_imm,
	[T32_16_B_T1] = exec_b_cond,
	[T32_16_B_T2] = exec_b,
	[T32_16_BX_T1] = exec_bx,
	[T32_16_NOP_T1] = exec_nop,
	[T32_16_WFI_T1] = exec_wfi,
};

Std_ReturnType thumb_execute(TargetCoreType *core)
{
	uint16 insn;
	uint32 next = core->reg.pc + 2U;
	T32Decode16Id id;
	Std_ReturnType err;

	if (bus_get_data16(core->core_id, core->reg.pc, &insn) != STD_E_OK) {
		core->access_fault = TRUE;
		return STD_E_SEGV;
	}
	id = t32_decode16(insn);
	if (executors[id] == NULL) {
		return STD_E_INVALID;
	}
	err = executors[id](core, insn, &next);
	if (err != STD_E_OK) {
		core->access_fault = TRUE;
		return err;
	}
	core->reg.pc = next;
	return STD_E_OK;
}

const char *thumb_instruction_name(uint16 insn)
{
	return t32_decode16_names[t32_decode16(insn)];
}

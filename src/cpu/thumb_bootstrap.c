/*
 * Bootstrap Thumb decoder and executor: the 16-bit instructions a bare-metal
 * hello world needs, hand-written to bring the target up. It is replaced by
 * the decoder generated from Arm's AARCHMRS data (docs/design.md, decision 10).
 *
 * Encodings: Arm Architecture Reference Manual, Armv7-M (Thumb, 16-bit).
 */
#include "cpu.h"
#include "bus.h"
#include "std_errno.h"

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

/* AddWithCarry() of the architecture, setting N, Z, C and V. */
static uint32 add_with_carry(TargetCoreType *core, uint32 x, uint32 y, uint32 carry_in)
{
	uint64 unsigned_sum = (uint64)x + (uint64)y + carry_in;
	sint64 signed_sum = (sint64)(sint32)x + (sint64)(sint32)y + (sint64)carry_in;
	uint32 result = (uint32)unsigned_sum;

	set_nz(core, result);
	core->reg.apsr &= ~(APSR_C | APSR_V);
	if ((uint64)result != unsigned_sum) {
		core->reg.apsr |= APSR_C;
	}
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
	uint16 v16;
	Std_ReturnType err;

	if (size == 1U) {
		err = bus_get_data8(core->core_id, address, &v8);
		*value = v8;
	}
	else if (size == 2U) {
		err = bus_get_data16(core->core_id, address, &v16);
		*value = v16;
	}
	else {
		err = bus_get_data32(core->core_id, address, value);
	}
	return err;
}

static Std_ReturnType store(TargetCoreType *core, uint32 address, uint32 size, uint32 value)
{
	if (size == 1U) {
		return bus_put_data8(core->core_id, address, (uint8)value);
	}
	if (size == 2U) {
		return bus_put_data16(core->core_id, address, (uint16)value);
	}
	return bus_put_data32(core->core_id, address, value);
}

Std_ReturnType thumb_execute(TargetCoreType *core)
{
	uint16 insn;
	uint32 pc = core->reg.pc;
	uint32 next = pc + 2U;
	uint32 base = (pc + 4U) & ~3U;	/* Align(PC, 4) for literal and ADR */
	uint32 value;
	Std_ReturnType err = STD_E_OK;

	if (bus_get_data16(core->core_id, pc, &insn) != STD_E_OK) {
		return STD_E_SEGV;
	}
	if ((insn & 0xF800U) == 0x4800U) {
		/* LDR (literal) T1: LDR Rt, [PC, #imm8*4] */
		err = load(core, base + ((uint32)(insn & 0xFFU) << 2), 4U, &value);
		core->reg.r[(insn >> 8) & 7U] = value;
	}
	else if ((insn & 0xF800U) == 0xA000U) {
		/* ADR T1: ADR Rd, #imm8*4 */
		core->reg.r[(insn >> 8) & 7U] = base + ((uint32)(insn & 0xFFU) << 2);
	}
	else if ((insn & 0xE000U) == 0x6000U || (insn & 0xF000U) == 0x7000U) {
		/* LDR/STR (immediate) T1 and LDRB/STRB (immediate) T1 */
		uint32 byte = ((insn & 0xF000U) == 0x7000U) ? 1U : 0U;
		uint32 imm5 = (insn >> 6) & 0x1FU;
		uint32 address = core->reg.r[(insn >> 3) & 7U] + (byte ? imm5 : (imm5 << 2));
		uint32 rt = insn & 7U;

		if ((insn & 0x0800U) != 0U) {
			err = load(core, address, byte ? 1U : 4U, &value);
			core->reg.r[rt] = value;
		}
		else {
			err = store(core, address, byte ? 1U : 4U, core->reg.r[rt]);
		}
	}
	else if ((insn & 0xE000U) == 0x2000U) {
		/* MOVS, CMP, ADDS, SUBS (8-bit immediate) T1/T2 */
		uint32 r = (insn >> 8) & 7U;
		uint32 imm8 = insn & 0xFFU;

		switch ((insn >> 11) & 3U) {
		case 0: core->reg.r[r] = imm8; set_nz(core, imm8); break;	/* MOVS (C unchanged) */
		case 1: (void)add_with_carry(core, core->reg.r[r], ~imm8, 1U); break;	/* CMP */
		case 2: core->reg.r[r] = add_with_carry(core, core->reg.r[r], imm8, 0U); break;	/* ADDS */
		default: core->reg.r[r] = add_with_carry(core, core->reg.r[r], ~imm8, 1U); break;	/* SUBS */
		}
	}
	else if ((insn & 0xF800U) == 0x0000U) {
		/* LSLS (immediate) T1; imm5 = 0 is MOVS (register) */
		uint32 imm5 = (insn >> 6) & 0x1FU;
		uint32 rm = core->reg.r[(insn >> 3) & 7U];

		if (imm5 != 0U) {
			core->reg.apsr &= ~APSR_C;
			if (((rm >> (32U - imm5)) & 1U) != 0U) {
				core->reg.apsr |= APSR_C;
			}
			rm <<= imm5;
		}
		core->reg.r[insn & 7U] = rm;
		set_nz(core, rm);
	}
	else if ((insn & 0xF000U) == 0xD000U && (insn & 0x0E00U) != 0x0E00U) {
		/* B<cond> T1 */
		if (condition_passed(core, (insn >> 8) & 0xFU)) {
			next = pc + 4U + (uint32)((sint32)(sint8)(insn & 0xFFU) << 1);
		}
	}
	else if ((insn & 0xF800U) == 0xE000U) {
		/* B T2 */
		sint32 offset = (sint32)((uint32)(insn & 0x7FFU) << 21) >> 20;
		next = pc + 4U + (uint32)offset;
	}
	else if ((insn & 0xFF87U) == 0x4700U) {
		/* BX Rm (Thumb state only) */
		next = core->reg.r[(insn >> 3) & 0xFU] & ~1U;
	}
	else if (insn == 0xBF00U) {
		/* NOP */
	}
	else if (insn == 0xBF30U) {
		/* WFI: no interrupt sources yet, so the core stays halted */
		core->is_halt = TRUE;
	}
	else {
		return STD_E_INVALID;
	}
	if (err != STD_E_OK) {
		core->access_fault = TRUE;
		return err;
	}
	core->reg.pc = next;
	return STD_E_OK;
}

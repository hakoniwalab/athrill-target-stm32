/*
 * Fetch and dispatch of Thumb instructions: a halfword whose bits 15:11 are
 * 0b11101, 0b11110 or 0b11111 starts a 32-bit instruction.
 */
#include "thumb_exec.h"
#include "t32_decode16.h"
#include "t32_decode32.h"

static Std_ReturnType fetch(const TargetCoreType *core, uint32 address, uint32 *insn, uint32 *length)
{
	uint16 hw1;
	uint16 hw2;

	if (bus_get_data16(core->core_id, address, &hw1) != STD_E_OK) {
		return STD_E_SEGV;
	}
	if (t32_decode16(hw1) != T32_16_WIDE) {
		*insn = hw1;
		*length = 2U;
		return STD_E_OK;
	}
	if (bus_get_data16(core->core_id, address + 2U, &hw2) != STD_E_OK) {
		return STD_E_SEGV;
	}
	*insn = ((uint32)hw1 << 16) | hw2;
	*length = 4U;
	return STD_E_OK;
}

Std_ReturnType thumb_execute(TargetCoreType *core)
{
	uint32 insn;
	uint32 length;
	Std_ReturnType err;

	err = fetch(core, core->reg.pc, &insn, &length);
	if (err != STD_E_OK) {
		core->access_fault = TRUE;
		return err;
	}
	core->reg.next_pc = core->reg.pc + length;
	core->it_written = FALSE;
	if (length == 2U) {
		err = thumb_execute16(core, (uint16)insn);
	}
	else {
		err = thumb_execute32(core, insn);
	}
	if (err == STD_E_SEGV) {
		core->access_fault = TRUE;
	}
	if (err != STD_E_OK) {
		return err;
	}
	/* ITAdvance() after every instruction but IT, whether its condition
	 * passed or not. */
	if (!core->it_written) {
		core->reg.itstate = ArmITAdvance(core->reg.itstate);
	}
	core->reg.pc = core->reg.next_pc;
	return STD_E_OK;
}

const char *thumb_instruction_at(const TargetCoreType *core, uint32 address, uint32 *insn, uint32 *length)
{
	*insn = 0U;
	*length = 2U;
	if (fetch(core, address, insn, length) != STD_E_OK) {
		return "(not readable)";
	}
	if (*length == 2U) {
		return t32_decode16_names[t32_decode16((uint16)*insn)];
	}
	return t32_decode32_names[t32_decode32(*insn)];
}

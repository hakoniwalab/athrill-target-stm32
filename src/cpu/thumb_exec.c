/*
 * Fetch and dispatch of Thumb instructions: a halfword whose bits 15:11 are
 * 0b11101, 0b11110 or 0b11111 starts a 32-bit instruction.
 */
#include "thumb_exec.h"
#include "t32_decode16.h"
#include "t32_decode32.h"
#include "arm_common/arm_m_exception.h"

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

/* ExecuteFPCheck() (arm_profile.h): the lazy floating-point context of the M profile. */
Std_ReturnType ArmExecuteFPCheck(TargetCoreType *core)
{
	return ArmMExecuteFPCheck(core);
}

/*
 * Ends a step that takes an exception: the next instruction is its handler.
 */
static Std_ReturnType take(TargetCoreType *core, Std_ReturnType err)
{
	if (err != STD_E_OK) {
		return err;
	}
	core->reg.pc = core->reg.next_pc;
	return STD_E_OK;
}

/*
 * One step: an exception to take (the highest-priority pending exception
 * that preempts, or the UsageFault of EPSR.T clear) or the instruction at PC.
 * After an instruction come what it asked for: an exception return (an
 * EXC_RETURN value written to PC in Handler mode) or a synchronous exception
 * (SVCall, a fault), in that order, after the IT state has advanced.
 */
Std_ReturnType thumb_execute(TargetCoreType *core)
{
	uint32 insn;
	uint32 length;
	uint32 n;
	Std_ReturnType err;

	if (core->m.lockup) {
		return STD_E_EXEC;
	}
	n = ArmMPendingToTakeFast(&core->m);
	if (n != 0U) {
		return take(core, ArmMExceptionEntry(core, n, core->reg.pc));
	}
	if (!core->m.epsr_t) {
		core->m.cfsr |= ARM_M_UFSR_INVSTATE << 16;
		return take(core, ArmMTakeSynchronous(core, ARM_M_EXC_USAGEFAULT, core->reg.pc));
	}
	err = fetch(core, core->reg.pc, &insn, &length);
	if (err != STD_E_OK) {
		core->access_fault = TRUE;
		return err;
	}
	core->reg.next_pc = core->reg.pc + length;
	core->it_written = FALSE;
	core->sync_exception = 0U;
	core->exc_return_pending = FALSE;
	if (length == 2U) {
		err = thumb_execute16(core, (uint16)insn);
	}
	else {
		err = thumb_execute32(core, insn);
	}
	if (err == STD_E_DECODE) {
		/* UNDEFINED (no executor, or an UNDEFINED case of an encoding): UsageFault UNDEFINSTR */
		core->m.cfsr |= ARM_M_UFSR_UNDEFINSTR << 16;
		return take(core, ArmMTakeSynchronous(core, ARM_M_EXC_USAGEFAULT, core->reg.pc));
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
	if (core->exc_return_pending) {
		err = ArmMExceptionReturn(core, core->exc_return);
		if (err != STD_E_OK) {
			return err;
		}
	}
	if (core->sync_exception != 0U) {
		return take(core, ArmMTakeSynchronous(core, core->sync_exception, core->sync_return));
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

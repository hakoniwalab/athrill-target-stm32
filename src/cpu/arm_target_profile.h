/*
 * The M profile for athrill-arm-common's pseudo-code (its arm_profile.h):
 * Armv7-M, Thumb state only.
 *
 * Not yet modelled: exceptions (a fault stops the run with its error instead
 * of entering a handler, and there is no exception return through PC), the
 * MPU, and the IT state (instructions other than conditional branches are
 * unconditional).
 */
#ifndef ATHRILL_STM32_ARM_TARGET_PROFILE_H
#define ATHRILL_STM32_ARM_TARGET_PROFILE_H

#include "target_cpu.h"
#include "bus.h"
#include "std_errno.h"
#include "arm_common/arm_types.h"

static inline uint32 ArmR(const TargetCoreType *core, uint32 n)
{
	return core->reg.r[n];
}

static inline void ArmRWrite(TargetCoreType *core, uint32 n, uint32 value)
{
	core->reg.r[n] = value;
}

/* Thumb: the instruction's address + 4. */
static inline uint32 ArmPC(const TargetCoreType *core)
{
	return core->reg.pc + 4U;
}

static inline uint32 ArmAPSR(const TargetCoreType *core)
{
	return core->reg.apsr;
}

static inline void ArmAPSRWrite(TargetCoreType *core, uint32 apsr)
{
	core->reg.apsr = apsr & (ARM_APSR_N | ARM_APSR_Z | ARM_APSR_C | ARM_APSR_V | ARM_APSR_Q | ARM_APSR_GE);
}

/*
 * BranchWritePC(bits(32) address)
 *     BranchTo(address<31:1>:'0');
 */
static inline Std_ReturnType ArmBranchWritePC(TargetCoreType *core, uint32 address)
{
	core->reg.next_pc = address & ~1U;
	return STD_E_OK;
}

/*
 * BXWritePC(bits(32) address)
 *     if CurrentMode == Mode_Handler && address<31:28> == '1111' then
 *         ExceptionReturn(address<27:0>);
 *     else
 *         EPSR.T = address<0>;   // if EPSR.T == 0, a UsageFault('Invalid State')
 *                                // is taken on the next instruction
 *         BranchTo(address<31:1>:'0');
 *
 * Without exceptions, the core is always in Thread mode, and a branch to an
 * address with bit 0 clear stops the run (STD_E_EXEC) where the UsageFault
 * would be taken.
 */
static inline Std_ReturnType ArmBXWritePC(TargetCoreType *core, uint32 address)
{
	if ((address & 1U) == 0U) {
		return STD_E_EXEC;
	}
	core->reg.next_pc = address & ~1U;
	return STD_E_OK;
}

/* LoadWritePC(bits(32) address): BXWritePC(address); */
static inline Std_ReturnType ArmLoadWritePC(TargetCoreType *core, uint32 address)
{
	return ArmBXWritePC(core, address);
}

/* ALUWritePC(bits(32) address): BranchWritePC(address); */
static inline Std_ReturnType ArmALUWritePC(TargetCoreType *core, uint32 address)
{
	return ArmBranchWritePC(core, address);
}

/* MemU[]: Armv7-M allows unaligned word and halfword accesses to normal memory. */
static inline Std_ReturnType ArmMemU_R(TargetCoreType *core, uint32 address, uint32 size, uint32 *value)
{
	uint8 v8;
	uint16 v16;
	uint32 v;
	uint32 i;
	Std_ReturnType err;

	if ((address & (size - 1U)) == 0U) {
		if (size == 1U) {
			err = bus_get_data8(core->core_id, address, &v8);
			*value = v8;
			return err;
		}
		if (size == 2U) {
			err = bus_get_data16(core->core_id, address, &v16);
			*value = v16;
			return err;
		}
		return bus_get_data32(core->core_id, address, value);
	}
	v = 0U;
	for (i = 0U; i < size; i++) {
		err = bus_get_data8(core->core_id, address + i, &v8);
		if (err != STD_E_OK) {
			return err;
		}
		v |= (uint32)v8 << (8U * i);
	}
	*value = v;
	return STD_E_OK;
}

static inline Std_ReturnType ArmMemU_W(TargetCoreType *core, uint32 address, uint32 size, uint32 value)
{
	uint32 i;
	Std_ReturnType err;

	if ((address & (size - 1U)) == 0U) {
		if (size == 1U) {
			return bus_put_data8(core->core_id, address, (uint8)value);
		}
		if (size == 2U) {
			return bus_put_data16(core->core_id, address, (uint16)value);
		}
		return bus_put_data32(core->core_id, address, value);
	}
	for (i = 0U; i < size; i++) {
		err = bus_put_data8(core->core_id, address + i, (uint8)(value >> (8U * i)));
		if (err != STD_E_OK) {
			return err;
		}
	}
	return STD_E_OK;
}

/* MemA[]: an unaligned access is a UsageFault (UNALIGNED); it stops the run. */
static inline Std_ReturnType ArmMemA_R(TargetCoreType *core, uint32 address, uint32 size, uint32 *value)
{
	if ((address & (size - 1U)) != 0U) {
		return STD_E_SEGV;
	}
	return ArmMemU_R(core, address, size, value);
}

static inline Std_ReturnType ArmMemA_W(TargetCoreType *core, uint32 address, uint32 size, uint32 value)
{
	if ((address & (size - 1U)) != 0U) {
		return STD_E_SEGV;
	}
	return ArmMemU_W(core, address, size, value);
}

/*
 * CurrentCond() of an instruction that is not a conditional branch: the IT
 * state's condition. IT is not executed yet, so it is always AL, and
 * InITBlock() is always FALSE.
 */
static inline uint32 Stm32CurrentCond(const TargetCoreType *core)
{
	(void)core;
	return ARM_COND_AL;
}

static inline bool Stm32InITBlock(const TargetCoreType *core)
{
	(void)core;
	return FALSE;
}

#endif /* ATHRILL_STM32_ARM_TARGET_PROFILE_H */

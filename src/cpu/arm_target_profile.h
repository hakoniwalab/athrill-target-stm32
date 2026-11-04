/*
 * The M profile for athrill-arm-common's pseudo-code (its arm_profile.h):
 * Armv7-M, Thumb state only.
 *
 * Exceptions are the M profile's model (athrill-arm-common arm_m_exception.h,
 * state in core->m): thumb_execute takes them between instructions. Not yet
 * modelled: the MPU, and the faults of memory accesses (a bus error or an
 * unaligned access stops the run instead of a BusFault or UsageFault).
 */
#ifndef ATHRILL_STM32_ARM_TARGET_PROFILE_H
#define ATHRILL_STM32_ARM_TARGET_PROFILE_H

#include <string.h>
#include "memory_window.h"
#include "target_cpu.h"
#include "bus.h"
#include "std_errno.h"
#include "arm_common/arm_types.h"
#include "arm_common/arm_helpers.h"

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

/* Armv7-M executes Thumb instructions only. */
static inline ArmInstrSet ArmCurrentInstrSet(const TargetCoreType *core)
{
	(void)core;
	return InstrSet_Thumb;
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
 * The exception return is done when the instruction completes
 * (thumb_execute: ArmMExceptionReturn).
 */
static inline Std_ReturnType ArmBXWritePC(TargetCoreType *core, uint32 address)
{
	if (core->m.handler_mode && ((address & 0xF0000000U) == 0xF0000000U)) {
		core->exc_return_pending = TRUE;
		core->exc_return = address;
		return STD_E_OK;
	}
	core->m.epsr_t = (address & 1U) != 0U;
	core->reg.next_pc = address & ~1U;
	return STD_E_OK;
}

/*
 * BLXWritePC(bits(32) address)
 *     EPSR.T = address<0>;   // if EPSR.T == 0, a UsageFault('Invalid State')
 *                            // is taken on the next instruction
 *     BranchTo(address<31:1>:'0');
 */
static inline Std_ReturnType ArmBLXWritePC(TargetCoreType *core, uint32 address)
{
	core->m.epsr_t = (address & 1U) != 0U;
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

/*
 * MemU[]: Armv7-M allows unaligned word and halfword accesses to normal
 * memory. Plain memory is read and written in the host memory directly
 * (memory_window.h; the host is little-endian, as Athrill's memory is).
 */
static inline Std_ReturnType ArmMemU_R(TargetCoreType *core, uint32 address, uint32 size, uint32 *value)
{
	uint8 v8;
	uint16 v16;
	uint32 v;
	uint32 i;
	Std_ReturnType err;
	const uint8 *p = memory_window_pointer(address, size);

	if (p != NULL) {
		if (size == 4U) {
			memcpy(value, p, 4U);
		}
		else if (size == 2U) {
			memcpy(&v16, p, 2U);
			*value = v16;
		}
		else {
			*value = *p;
		}
		return STD_E_OK;
	}
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
	uint8 *p = memory_window_write_pointer(address, size);
	uint16 v16;

	if (p != NULL) {
		if (size == 4U) {
			memcpy(p, &value, 4U);
		}
		else if (size == 2U) {
			v16 = (uint16)value;
			memcpy(p, &v16, 2U);
		}
		else {
			*p = (uint8)value;
		}
		return STD_E_OK;
	}
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

/* MemU_unpriv[]: without an MPU, unprivileged accesses are ordinary ones. */
static inline Std_ReturnType ArmMemU_unpriv_R(TargetCoreType *core, uint32 address, uint32 size, uint32 *value)
{
	return ArmMemU_R(core, address, size, value);
}

static inline Std_ReturnType ArmMemU_unpriv_W(TargetCoreType *core, uint32 address, uint32 size, uint32 value)
{
	return ArmMemU_W(core, address, size, value);
}

/* IntegerZeroDivideTrappingEnabled(): CCR.DIV_0_TRP. */
static inline bool ArmIntegerZeroDivideTrappingEnabled(const TargetCoreType *core)
{
	return (core->m.ccr & ARM_M_CCR_DIV_0_TRP) != 0U;
}

/* GenerateIntegerZeroDivide(): a UsageFault (DIVBYZERO), taken after the instruction (thumb_execute). */
static inline Std_ReturnType ArmGenerateIntegerZeroDivide(TargetCoreType *core)
{
	core->m.cfsr |= ARM_M_UFSR_DIVBYZERO << 16;
	core->sync_exception = ARM_M_EXC_USAGEFAULT;
	core->sync_return = core->reg.pc;
	return STD_E_OK;
}

/*
 * The local exclusive monitor (one core): LDREX marks an address, STREX to
 * it passes once, CLREX clears it. Exception entry and return will clear it
 * too when exceptions are modelled.
 */
static inline void ArmSetExclusiveMonitors(TargetCoreType *core, uint32 address, uint32 size)
{
	(void)size;
	core->exclusive_valid = TRUE;
	core->exclusive_address = address;
}

/*
 * ExclusiveMonitorsPass(address, size): an unaligned address is an
 * alignment fault (UsageFault UNALIGNED, which stops the run).
 */
static inline Std_ReturnType ArmExclusiveMonitorsPass(TargetCoreType *core, uint32 address, uint32 size, bool *pass)
{
	if ((address & (size - 1U)) != 0U) {
		return STD_E_SEGV;
	}
	*pass = core->exclusive_valid && (core->exclusive_address == address);
	if (*pass) {
		core->exclusive_valid = FALSE;
	}
	return STD_E_OK;
}

static inline void ArmClearExclusiveLocal(TargetCoreType *core)
{
	core->exclusive_valid = FALSE;
}

/*
 * The FPU (FPv5-D16): S0-S31, D0-D15 over them, and FPSCR. The coprocessor
 * access control (CPACR) is not modelled: the FPU is always enabled.
 */
static inline uint32 ArmS(const TargetCoreType *core, uint32 n)
{
	return core->reg.s[n];
}

static inline void ArmSWrite(TargetCoreType *core, uint32 n, uint32 value)
{
	core->reg.s[n] = value;
}

static inline uint64 ArmD(const TargetCoreType *core, uint32 n)
{
	return ((uint64)core->reg.s[2U * n + 1U] << 32) | core->reg.s[2U * n];
}

static inline void ArmDWrite(TargetCoreType *core, uint32 n, uint64 value)
{
	core->reg.s[2U * n] = (uint32)value;
	core->reg.s[2U * n + 1U] = (uint32)(value >> 32);
}

/* FPSCR of FPv5: N Z C V (31:28), AHP (26), DN (25), FZ (24), RMode (23:22)
 * and the cumulative exception bits IDC (7), IXC, UFC, OFC, DZC, IOC (4:0). */
#define STM32_FPSCR_MASK	0xF7C0009FU

/* ExecuteFPCheck(): the M profile's lazy context preservation (thumb_exec.c). */
Std_ReturnType ArmExecuteFPCheck(TargetCoreType *core);

static inline uint32 ArmFPSCR(const TargetCoreType *core)
{
	return core->reg.fpscr;
}

/* The floating-point mode (DEVICE_CONFIG_ARM_FP_MODE): fast (the host's FPU where exact) or strict. */
extern std_bool stm32_fp_host_fast;

static inline bool ArmFPHostFast(const TargetCoreType *core)
{
	(void)core;
	return stm32_fp_host_fast;
}

static inline void ArmFPSCRWrite(TargetCoreType *core, uint32 value)
{
	core->reg.fpscr = value & STM32_FPSCR_MASK;
}

/* ITSTATE.IT<7:0> = itstate (IT); thumb_execute does not advance it after IT. */
static inline void ArmITStateWrite(TargetCoreType *core, uint32 itstate)
{
	core->reg.itstate = itstate & 0xFFU;
	core->it_written = TRUE;
}

static inline uint32 ArmIPSR(const TargetCoreType *core)
{
	return core->m.ipsr;
}

static inline uint32 ArmITState(const TargetCoreType *core)
{
	return core->reg.itstate;
}

static inline uint32 ArmNextPC(const TargetCoreType *core)
{
	return core->reg.next_pc;
}

static inline ArmMStateType *ArmMState(const TargetCoreType *core)
{
	return (ArmMStateType *)&core->m;
}

/* CallSupervisor(): SVCall, taken after the instruction (thumb_execute). */
static inline void ArmCallSupervisor(TargetCoreType *core)
{
	core->sync_exception = ARM_M_EXC_SVCALL;
	core->sync_return = core->reg.next_pc;
}

/* The Cortex-M7 has the DSP extension. */
static inline bool ArmHaveDSPExt(const TargetCoreType *core)
{
	(void)core;
	return TRUE;
}

/*
 * CurrentCond() of an instruction that is not a conditional branch: the IT
 * state's condition in an IT block, AL outside one.
 */
static inline uint32 Stm32CurrentCond(const TargetCoreType *core)
{
	if (ArmInITBlock(core->reg.itstate)) {
		return ArmITCondition(core->reg.itstate);
	}
	return ARM_COND_AL;
}

static inline bool Stm32InITBlock(const TargetCoreType *core)
{
	return ArmInITBlock(core->reg.itstate);
}

static inline bool Stm32LastInITBlock(const TargetCoreType *core)
{
	return ArmLastInITBlock(core->reg.itstate);
}

#endif /* ATHRILL_STM32_ARM_TARGET_PROFILE_H */

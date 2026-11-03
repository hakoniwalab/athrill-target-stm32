/*
 * The core's System Control Space: its registers are athrill-arm-common's
 * arm_m_scs.h over the exception model in the core (core->m, core->systick,
 * core->scs).
 *
 * SysTick advances with the clock as the RISC-V target's mtime does: from
 * the clock it was last brought to, on each device tick and before each
 * register access (a skip of the halted CPU moves the clock by many clocks
 * at once). Its clock is the processor clock (CSR.CLKSOURCE = 1) or, on the
 * STM32, the processor clock divided by 8.
 */
#include "arm_m_scs_device.h"
#include "device.h"
#include "cpu.h"
#include "cpuemu_ops.h"
#include "arm_common/arm_m_scs.h"

#define STM32_SYSTICK_EXTERNAL_DIVIDER	8U

uint8 arm_m_scs_data[ARM_M_SCS_DEVICE_SIZE];

static uint64 last_clock;
static uint64 remainder;

static TargetCoreType *scs_core(void)
{
	return &virtual_cpu.cores[0].core;
}

static uint32 systick_divider(const TargetCoreType *core)
{
	return ((core->systick.csr & ARM_M_SYST_CLKSOURCE) != 0U) ? 1U : STM32_SYSTICK_EXTERNAL_DIVIDER;
}

/* SysTick up to the given clock. */
static void advance_to(uint64 clock)
{
	TargetCoreType *core = scs_core();
	uint64 elapsed;
	uint32 divider = systick_divider(core);

	if (clock <= last_clock) {
		return;
	}
	elapsed = (clock - last_clock) + remainder;
	last_clock = clock;
	ArmMSysTickClock(&core->m, &core->systick, elapsed / divider);
	remainder = elapsed % divider;
}

void arm_m_scs_reset(void)
{
	last_clock = 0U;
	remainder = 0U;
}

void arm_m_scs_tick(DeviceClockType *clock)
{
	TargetCoreType *core = scs_core();
	uint64 ticks;
	uint64 until;

	advance_to(clock->clock);
	ticks = ArmMSysTickTicksToInterrupt(&core->systick);
	if (ticks == 0U) {
		return;
	}
	until = (ticks * systick_divider(core)) - remainder;
	if (until < clock->min_intr_interval) {
		clock->min_intr_interval = (until == 0U) ? 1U : until;
	}
}

static uint32 offset_of(MpuAddressRegionType *r, AthrillAddrType a)
{
	return (uint32)(a - r->start);
}

static uint32 read_register(MpuAddressRegionType *r, AthrillAddrType a, uint32 size)
{
	TargetCoreType *core = scs_core();
	uint32 offset = offset_of(r, a);
	uint32 value;

	advance_to(cpuemu_get_clock());
	value = ArmMScsRead(&core->m, &core->systick, &core->scs, offset, size);
	ArmMScsReadSideEffects(&core->systick, offset);
	return value;
}

static void write_register(MpuAddressRegionType *r, AthrillAddrType a, uint32 size, uint32 value)
{
	TargetCoreType *core = scs_core();

	advance_to(cpuemu_get_clock());
	ArmMScsWrite(core, &core->m, &core->systick, &core->scs, offset_of(r, a), size, value);
}

static Std_ReturnType get8(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 *d)
{
	(void)c;
	stm32_device_accessed = TRUE;
	*d = (uint8)read_register(r, a, 1U);
	return STD_E_OK;
}

static Std_ReturnType get16(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint16 *d)
{
	(void)c;
	stm32_device_accessed = TRUE;
	*d = (uint16)read_register(r, a, 2U);
	return STD_E_OK;
}

static Std_ReturnType get32(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint32 *d)
{
	(void)c;
	stm32_device_accessed = TRUE;
	*d = read_register(r, a, 4U);
	return STD_E_OK;
}

static Std_ReturnType put8(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 d)
{
	(void)c;
	stm32_device_accessed = TRUE;
	write_register(r, a, 1U, d);
	return STD_E_OK;
}

static Std_ReturnType put16(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint16 d)
{
	(void)c;
	stm32_device_accessed = TRUE;
	write_register(r, a, 2U, d);
	return STD_E_OK;
}

static Std_ReturnType put32(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint32 d)
{
	(void)c;
	stm32_device_accessed = TRUE;
	write_register(r, a, 4U, d);
	return STD_E_OK;
}

static Std_ReturnType get_pointer(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 **d)
{
	(void)c;
	*d = &r->data[a - r->start];
	return STD_E_OK;
}

MpuAddressRegionOperationType arm_m_scs_ops = { get8, get16, get32, put8, put16, put32, get_pointer };

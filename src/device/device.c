#include "device.h"
#include "cpuemu_ops.h"
#include "peripheral/arm_m_scs_device.h"
#include "peripheral/stm32_periph_probe.h"
#include "peripheral/stm32_tim_probe.h"
#ifdef EXDEV_ENABLE
#include "athrill_device.h"
#endif

#ifdef EXDEV_ENABLE
static std_bool exdev_initialized = FALSE;
#endif

std_bool stm32_device_accessed = FALSE;

void device_init(CpuType *cpu, DeviceClockType *clock)
{
	(void)cpu;
	clock->clock = 0U;
	clock->intclock = 0U;
	clock->min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	clock->can_skip_clock = TRUE;
	clock->is_halt = FALSE;
	arm_m_scs_reset();
	stm32_probe_init();
	stm32_tim_probe_reset();
}

void device_supply_clock(DeviceClockType *clock)
{
	clock->can_skip_clock = TRUE;
	clock->min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	arm_m_scs_tick(clock);
	stm32_tim_probe_tick(clock);
	stm32_probe_tick();
#ifdef EXDEV_ENABLE
	/* External devices (memory.txt DEV) are registered when the program is loaded. */
	if (!exdev_initialized) {
		device_init_athrill_exdev();
		exdev_initialized = TRUE;
	}
	device_supply_clock_exdev(clock);
#endif
}

void device_reset(CpuType *cpu, DeviceClockType *clock)
{
	device_init(cpu, clock);
}

void device_get_clock(DeviceClockType *clock)
{
	(void)clock;
}

/* An interrupt request of a device: interrupt intno pending in the NVIC. */
void device_raise_int(uint16 intno)
{
	(void)intc_raise_intr(intno);
}

int intc_raise_intr(uint32 intno)
{
	TargetCoreType *core = &virtual_cpu.cores[0].core;

	if (intno >= core->m.num_irq) {
		return -1;
	}
	core->m.pending[ARM_M_EXC_IRQ0 + intno] = 1U;
	core->m.pending_any = TRUE;
	return 0;
}

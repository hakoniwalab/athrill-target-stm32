#include "device.h"
#include "cpuemu_ops.h"
#ifdef EXDEV_ENABLE
#include "athrill_device.h"
#endif

#ifdef EXDEV_ENABLE
static std_bool exdev_initialized = FALSE;
#endif

void device_init(CpuType *cpu, DeviceClockType *clock)
{
	(void)cpu;
	clock->clock = 0U;
	clock->intclock = 0U;
	clock->min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	clock->can_skip_clock = TRUE;
	clock->is_halt = FALSE;
}

void device_supply_clock(DeviceClockType *clock)
{
	clock->can_skip_clock = TRUE;
	clock->min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
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

void device_raise_int(uint16 intno)
{
	(void)intno;
}

/* Interrupts from the core and the debugger; the NVIC is not modelled yet. */
int intc_raise_intr(uint32 intno)
{
	(void)intno;
	return -1;
}

#include "device.h"
#include <stdio.h>
#include "cpuemu_ops.h"
#include "peripheral/arm_m_scs_device.h"
#include "peripheral/arm_m_dwt.h"
#include "sensor/sensor_source.h"
#include "board/board_profile.h"
#include "device_tree.h"
#ifdef EXDEV_ENABLE
#include "athrill_device.h"
#endif

#ifdef EXDEV_ENABLE
static std_bool exdev_initialized = FALSE;
#endif

std_bool stm32_device_accessed = FALSE;

/*
 * The order device_supply_clock() ticks the devices in, written once: the
 * calls and the start-up log (TARGET_TICK lines; "devicetree" is where the
 * athrill-device models tick, in the order of its DEVICE_TICK lines). The
 * regression references record this order, so a change is reported.
 */
#define TARGET_TICKS(X) \
	X(arm_m_scs, arm_m_scs_tick(clock)) \
	X(devicetree, stm32_device_tree_tick(clock))

static void log_tick_order(void)
{
	static const char *const names[] = {
#define TICK_NAME(name, call)	#name,
		TARGET_TICKS(TICK_NAME)
#undef TICK_NAME
	};
	uint32 i;

	for (i = 0U; i < sizeof(names) / sizeof(names[0]); i++) {
		fprintf(stderr, "TARGET_TICK index=%u name=%s\n", i, names[i]);
	}
}

void device_init(CpuType *cpu, DeviceClockType *clock)
{
	(void)cpu;
	clock->clock = 0U;
	clock->intclock = 0U;
	clock->min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	clock->can_skip_clock = TRUE;
	clock->is_halt = FALSE;
	arm_m_scs_reset();
	arm_m_dwt_reset();
	sensor_source_reset();
	/* The board comes only from the Devicetree (DEVICE_CONFIG_DEVICE_TREE);
	 * without one, Athrill runs the CPU with these controllers and no board. */
	(void)stm32_device_tree_build();
	log_tick_order();
}

void device_supply_clock(DeviceClockType *clock)
{
	clock->can_skip_clock = TRUE;
	clock->min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
#define TICK_CALL(name, call)	call;
	TARGET_TICKS(TICK_CALL)
#undef TICK_CALL
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
	core->m.pending_check = TRUE;
	return 0;
}

std_bool intc_irq_active(uint32 intno)
{
	TargetCoreType *core = &virtual_cpu.cores[0].core;

	if (intno >= core->m.num_irq) {
		return FALSE;
	}
	return core->m.active[ARM_M_EXC_IRQ0 + intno] != 0U;
}

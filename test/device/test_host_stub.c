/*
 * Test stand-in for target_host.c: the required interface for the unit
 * tests of the target models. Each operation calls the core stand-in that
 * the test file defines (cpuemu_get_clock, intc_raise_intr, ...), so the
 * tests keep observing the same calls as before the models used
 * stm32_host_ops(). A test that does not define a stand-in gets 0 / no-op.
 * SPDX-License-Identifier: MIT
 */
#include "target_host.h"

#include <stdio.h>
#include <string.h>

extern uint64 cpuemu_get_clock(void) __attribute__((weak));
extern int intc_raise_intr(uint32 intno) __attribute__((weak));
extern std_bool intc_irq_active(uint32 intno) __attribute__((weak));
extern Std_ReturnType cpuemu_get_devcfg_value(const char *key, uint32 *value) __attribute__((weak));

static uint64 time_now(void *context)
{
	(void)context;
	return (cpuemu_get_clock != NULL) ? cpuemu_get_clock() : 0U;
}

static sint32 irq_set(void *context, AthrillDeviceIrqLine line, std_bool asserted)
{
	(void)context;
	if (asserted && (intc_raise_intr != NULL)) {
		return intc_raise_intr((uint32)line.value);
	}
	return 0;
}

static std_bool irq_is_active(void *context, AthrillDeviceIrqLine line)
{
	(void)context;
	return (intc_irq_active != NULL) ? intc_irq_active((uint32)line.value) : FALSE;
}

static sint32 host_value(void *context, const char *key, const char **value)
{
	static char text[32];
	uint32 number;
	(void)context;
	if ((cpuemu_get_devcfg_value == NULL) || (cpuemu_get_devcfg_value(key, &number) != STD_E_OK)) {
		return -1;
	}
	(void)snprintf(text, sizeof(text), "%u", number);
	*value = text;
	return 0;
}

static const AthrillDeviceTargetOps host_ops = {
	.abi_major = ATHRILL_DEVICE_ABI_MAJOR,
	.abi_minor = ATHRILL_DEVICE_ABI_MINOR,
	.struct_size = sizeof(AthrillDeviceTargetOps),
	.irq_set = irq_set,
	.irq_is_active = irq_is_active,
	.time_now = time_now,
	.host_value = host_value,
};

const AthrillDeviceTargetOps *stm32_host_ops(void)
{
	return &host_ops;
}

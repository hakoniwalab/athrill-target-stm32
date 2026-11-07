/*
 * Test stand-in for target_host.c: the required interface for the unit
 * tests of the target models. Each operation calls the core stand-in that
 * the test file defines (cpuemu_get_clock, intc_raise_intr, ...), so the
 * tests keep observing the same calls as before the models used
 * athrill_device_host(). Memory goes to the mpu_get/put_data stand-ins (what the
 * core bus_get/put_data inline functions call). A test that does not define a
 * stand-in gets 0 / no-op.
 * SPDX-License-Identifier: MIT
 */
#include "target_host.h"

#include <stdio.h>
#include <string.h>

extern uint64 cpuemu_get_clock(void) __attribute__((weak));
extern int intc_raise_intr(uint32 intno) __attribute__((weak));
extern std_bool intc_irq_active(uint32 intno) __attribute__((weak));
extern Std_ReturnType cpuemu_get_devcfg_value(const char *key, uint32 *value) __attribute__((weak));
extern Std_ReturnType mpu_get_data8(CoreIdType core_id, AthrillAddrType addr, uint8 *data) __attribute__((weak));
extern Std_ReturnType mpu_get_data16(CoreIdType core_id, AthrillAddrType addr, uint16 *data) __attribute__((weak));
extern Std_ReturnType mpu_get_data32(CoreIdType core_id, AthrillAddrType addr, uint32 *data) __attribute__((weak));
extern Std_ReturnType mpu_put_data8(CoreIdType core_id, AthrillAddrType addr, uint8 data) __attribute__((weak));
extern Std_ReturnType mpu_put_data16(CoreIdType core_id, AthrillAddrType addr, uint16 data) __attribute__((weak));
extern Std_ReturnType mpu_put_data32(CoreIdType core_id, AthrillAddrType addr, uint32 data) __attribute__((weak));

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

/* Same widths as target_host.c: 1, 2 or 4 bytes in one access, else bytes. */
static sint32 memory_read(void *context, uint64 address, void *data, uint32 size)
{
	uint32 i;
	(void)context;
	if ((size == 2U) && (mpu_get_data16 != NULL)) {
		uint16 v16 = 0U;
		(void)mpu_get_data16(0U, (AthrillAddrType)address, &v16);
		memcpy(data, &v16, 2U);
		return 0;
	}
	if ((size == 4U) && (mpu_get_data32 != NULL)) {
		uint32 v32 = 0U;
		(void)mpu_get_data32(0U, (AthrillAddrType)address, &v32);
		memcpy(data, &v32, 4U);
		return 0;
	}
	for (i = 0U; (i < size) && (mpu_get_data8 != NULL); i++) {
		(void)mpu_get_data8(0U, (AthrillAddrType)(address + i), &((uint8 *)data)[i]);
	}
	return 0;
}

static sint32 memory_write(void *context, uint64 address, const void *data, uint32 size)
{
	uint32 i;
	(void)context;
	if ((size == 2U) && (mpu_put_data16 != NULL)) {
		uint16 v16;
		memcpy(&v16, data, 2U);
		(void)mpu_put_data16(0U, (AthrillAddrType)address, v16);
		return 0;
	}
	if ((size == 4U) && (mpu_put_data32 != NULL)) {
		uint32 v32;
		memcpy(&v32, data, 4U);
		(void)mpu_put_data32(0U, (AthrillAddrType)address, v32);
		return 0;
	}
	for (i = 0U; (i < size) && (mpu_put_data8 != NULL); i++) {
		(void)mpu_put_data8(0U, (AthrillAddrType)(address + i), ((const uint8 *)data)[i]);
	}
	return 0;
}

static const AthrillDeviceTargetOps host_ops = {
	.abi_major = ATHRILL_DEVICE_ABI_MAJOR,
	.abi_minor = ATHRILL_DEVICE_ABI_MINOR,
	.struct_size = sizeof(AthrillDeviceTargetOps),
	.irq_set = irq_set,
	.irq_is_active = irq_is_active,
	.time_now = time_now,
	.memory_read = memory_read,
	.memory_write = memory_write,
	.host_value = host_value,
};

const AthrillDeviceTargetOps *athrill_device_host(void)
{
	return &host_ops;
}

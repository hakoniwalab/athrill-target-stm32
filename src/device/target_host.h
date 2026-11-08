/*
 * The STM32 target's implementation of the athrill-device required
 * interface (AthrillDeviceTargetOps, operations marked REQUIRED in
 * athrill_device_if.h): MMIO regions, IRQ, time, clock, memory and host
 * configuration values.
 *
 * Responsibility: the CPU-side services only; every peripheral is an
 * athrill-device model (hakoniwalab/athrill-device#13), so the bridge
 * operations marked TO BE REMOVED in the table are all NULL.
 *
 * Caller: device_tree.c builds its AthrillDeviceTargetOps table from the
 * stm32_host_* functions below and reads configuration with
 * stm32_host_config_string().
 *
 * Copyright 2026 Hakoniwa Laboratory
 * SPDX-License-Identifier: MIT
 */
#ifndef ATHRILL_STM32_TARGET_HOST_H
#define ATHRILL_STM32_TARGET_HOST_H

#include "athrill_device_if.h"
#include "std_errno.h"


sint32 stm32_host_mmio_register(void *context, uint64 address, uint64 size,
	const AthrillDeviceMmioOps *ops, void *instance, AthrillDeviceMmioToken *token);
void stm32_host_mmio_unregister(void *context, AthrillDeviceMmioToken token);
sint32 stm32_host_irq_resolve(void *context, const AthrillDeviceSpecifier *spec,
	AthrillDeviceIrqLine *line);
sint32 stm32_host_irq_set(void *context, AthrillDeviceIrqLine line, std_bool asserted);
std_bool stm32_host_irq_is_active(void *context, AthrillDeviceIrqLine line);
uint64 stm32_host_time_now(void *context);
uint64 stm32_host_time_frequency_hz(void *context);
sint32 stm32_host_clock_frequency_hz(void *context, AthrillDeviceNodeId node,
	const char *clock_name, uint64 *frequency);
sint32 stm32_host_memory_read(void *context, uint64 address, void *data, uint32 size);
sint32 stm32_host_memory_write(void *context, uint64 address, const void *data, uint32 size);
sint32 stm32_host_value(void *context, const char *key, const char **value);
/* ABI 1.10: firmware console output (Athrill's debugger serial) and the
 * end of the CPU's current instruction batch. */
sint32 stm32_host_console_write(void *context, uint32 channel, uint8 data);
void stm32_host_end_batch(void *context);

/* A configuration string without the core's echo on stdout. */
Std_ReturnType stm32_host_config_string(const char *key, char **value);
/* Forget the numeric host values cached by stm32_host_value (new build). */
void stm32_host_reset_values(void);

#endif

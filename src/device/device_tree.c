/*
 * athrill-device target adapter for the STM32 target.
 *
 * Builds the Devicetree runtime (athrill_device_build) and hands it the full
 * AthrillDeviceTargetOps table:
 *   REQUIRED operations   implemented in target_host.c (stm32_host_*)
 *   TO BE REMOVED bridges all NULL: every peripheral, the GPIO lines and the
 *                         sensor source are athrill-device models now
 *                         (hakoniwalab/athrill-device#13)
 * Calls into target_host.c: the REQUIRED operations through the table,
 * stm32_host_value/stm32_host_config_string for host values and plugin
 * mappings, stm32_host_reset_values at each build.
 *
 * Copyright 2026 Hakoniwa Laboratory
 * SPDX-License-Identifier: MIT
 */
#include "device_tree.h"

#include "athrill_device_if.h"
#include "bus.h"
#include "cpuemu_ops.h"
#include "device.h"
#include "mpu.h"
#include "std_errno.h"
#include "target_cpu.h"
#include "arm_common/arm_m_types.h"
#include "target_host.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_PLUGINS 32U
#define MAX_HOST_VALUES 32U
#define MAX_PLUGIN_MAPPING 1024U

/* The target's own state for the runtime: nothing since the GPIO lines
 * moved into athrill-device (ABI 1.16); kept as the context pointer. */
typedef struct {
	uint32 unused;
} Stm32DeviceTreeTarget;

static Stm32DeviceTreeTarget target_context;
static AthrillDeviceRuntime *device_tree_runtime;
static std_bool destroy_registered;

static char plugin_mappings[MAX_PLUGINS][MAX_PLUGIN_MAPPING];

static const AthrillDeviceTargetOps target_ops = {
	ATHRILL_DEVICE_ABI_MAJOR, ATHRILL_DEVICE_ABI_MINOR, sizeof(AthrillDeviceTargetOps),
	/* REQUIRED operations: target_host.c */
	stm32_host_mmio_register, stm32_host_mmio_unregister, stm32_host_irq_resolve, stm32_host_irq_set,
	stm32_host_irq_is_active, stm32_host_time_now, stm32_host_time_frequency_hz,
	stm32_host_clock_frequency_hz, stm32_host_memory_read, stm32_host_memory_write,
	/* TO BE REMOVED bridge operations: all NULL (the layout of ABI 1.x) */
	NULL, NULL, NULL,	/* spi_bind, spi_set_chip_select, spi_exchange_byte: the SPI controllers are athrill-device models */
	NULL, NULL,	/* i2c_bind, i2c_transfer: the I2C controller is an athrill-device model */
	NULL, NULL, NULL,	/* uart_bind, uart_peripheral, uart_controller: the UARTs are athrill-device models */
	NULL, NULL, NULL, NULL,	/* gpio_resolve, gpio_read, gpio_drive, gpio_subscribe: the runtime's GPIO lines (ABI 1.16) */
	NULL, NULL,	/* dma_resolve, dma_request: the DMA is an athrill-device model on request lines */
	NULL /* model_adapter_connect: no adapters left */, NULL /* spi_bind_node */, stm32_host_value,
	NULL /* sensor_sample: the bench sensor source model */, NULL /* i2c_bind_node */, NULL /* uart_bind_nodes */,
	NULL /* uart_send_from_node */, NULL /* gps_sample */, NULL /* actuator_output */,
	NULL,	/* gpio_outputs: the runtime keeps the output levels */
	/* ABI 1.10 REQUIRED operations: target_host.c */
	stm32_host_console_write, stm32_host_end_batch
};

static uint32 collect_host_values(AthrillDeviceHostValue values[MAX_HOST_VALUES])
{
	static const char *const keys[] = {
		"DEVICE_CONFIG_STM32_FRAM_FILE", "DEVICE_CONFIG_STM32_SDCARD_IMAGE",
		"DEVICE_CONFIG_STM32_UART_RX_FILE", "DEVICE_CONFIG_STM32_UART_TX_FILE",
		"DEVICE_CONFIG_STM32_ACTUATOR_FILE", "DEVICE_CONFIG_STM32_SENSOR_BENCH",
		"DEVICE_CONFIG_STM32_SENSOR_DEVICE", "DEVICE_CONFIG_STM32_UART2_TCP_PORT",
		"HAKO_SENSOR_DEVICE_ASSET", "HAKO_SENSOR_DEVICE_ROBOT",
		"HAKO_SENSOR_DEVICE_SENSOR_CHANNEL", "HAKO_SENSOR_DEVICE_GPS_CHANNEL",
		"HAKO_SENSOR_DEVICE_ACTUATOR_CHANNEL", "HAKO_AIRCRAFT_CONFIG_PATH",
		"HAKO_AIRCRAFT_WORKER", "HAKO_SENSOR_DEVICE_TRACE_PATH"
	};
	uint32 count = 0U, i;
	const char *value;
	for (i = 0U; i < sizeof(keys) / sizeof(keys[0]) && count < MAX_HOST_VALUES; i++) {
		value = NULL;
		if (stm32_host_value(NULL, keys[i], &value) == 0) {
			values[count].key = keys[i];
			values[count].value = value;
			count++;
		}
	}
	return count;
}

static int collect_plugins(AthrillDevicePluginPath plugins[MAX_PLUGINS])
{
	uint32 i;
	char key[64];
	char *mapping;
	for (i = 0U; i < MAX_PLUGINS; i++) {
		char *equals;
		snprintf(key, sizeof(key), "DEVICE_CONFIG_DEVICE_PLUGIN_%u", i);
		mapping = NULL;
		if (stm32_host_config_string(key, &mapping) != STD_E_OK) {
			break;
		}
		if (strlen(mapping) >= sizeof(plugin_mappings[i])) {
			return -1;
		}
		strcpy(plugin_mappings[i], mapping);
		mapping = plugin_mappings[i];
		equals = strchr(mapping, '=');
		if (equals == NULL || equals == mapping || equals[1] == 0) {
			return -1;
		}
		*equals = 0;
		{
			char *encoded;
			while ((encoded = strstr(mapping, "%2C")) != NULL ||
			       (encoded = strstr(mapping, "%2c")) != NULL) {
				*encoded = ',';
				memmove(encoded + 1, encoded + 3, strlen(encoded + 3) + 1U);
			}
		}
		plugins[i].compatible = mapping;
		plugins[i].library_path = equals + 1;
	}
	return (int)i;
}

std_bool stm32_device_tree_build(void)
{
	static const char *const target_compatibles[] = {
		"arm,cortex-m7", "arm,v7m-nvic", "arm,armv7m-systick",
		"arm,armv7m-mpu", "arm,armv7m-dwt", "arm,dtcm"
	};
	AthrillDeviceBuildOptions options;
	AthrillDevicePluginPath plugins[MAX_PLUGINS];
	AthrillDeviceHostValue host_values[MAX_HOST_VALUES];
	char *path = NULL;
	char *legacy_value = NULL;
	int plugin_count;
	stm32_device_tree_destroy();
	stm32_host_reset_values();
	if (cpuemu_get_devcfg_string("DEVICE_CONFIG_DEVICE_TREE", &path) != STD_E_OK) {
		char *board = NULL;
		/* The boards built by C code are gone (hakoniwalab/athrill-device#13):
		 * a configuration that still names one must not run without a board. */
		if (stm32_host_config_string("DEVICE_CONFIG_STM32_BOARD", &board) == STD_E_OK) {
			printf("ERROR: DEVICE_CONFIG_STM32_BOARD %s needs DEVICE_CONFIG_DEVICE_TREE "
			       "(athrill-device boards/<board>/<board>.dtb); boards are built only from the Devicetree\n", board);
			exit(1);
		}
		return FALSE;
	}
	/* The C path makes these two configuration echoes while attaching its
	 * board and F-RAM.  The DT path consumes the same values through the graph;
	 * keep their one visible lookup so normalized consoles remain identical. */
	(void)cpuemu_get_devcfg_string("DEVICE_CONFIG_STM32_BOARD", &legacy_value);
	(void)cpuemu_get_devcfg_string("DEVICE_CONFIG_STM32_FRAM_FILE", &legacy_value);
	plugin_count = collect_plugins(plugins);
	if (plugin_count < 0) {
		printf("DEVICE_TREE_ERROR code=plugin-config node=/ property=DEVICE_CONFIG_DEVICE_PLUGIN reason=expected-compatible=path\n");
		exit(1);
	}
	memset(&options, 0, sizeof(options));
	options.dtb_path = path;
	options.plugins = plugins;
	options.plugin_count = (uint32)plugin_count;
	options.target_compatibles = target_compatibles;
	options.target_compatible_count = sizeof(target_compatibles) / sizeof(target_compatibles[0]);
	options.maximum_dtb_size = 16U * 1024U * 1024U;
	options.host_values = host_values;
	options.host_value_count = collect_host_values(host_values);
	memset(&target_context, 0, sizeof(target_context));
	if (athrill_device_build(&options, &target_ops, &target_context, &device_tree_runtime) != 0) {
		printf("ERROR: DEVICE_CONFIG_DEVICE_TREE build failed\n");
		exit(1);
	}
	/* The NVIC's size from its node (#62): the lines (athrill,num-irqs, which
	 * ICTR reports) and the implemented priority bits. */
	{
		uint32 num_irq, priority_bits;
		if (athrill_device_target_node_u32(device_tree_runtime, "arm,v7m-nvic", "athrill,num-irqs",
				0U, &num_irq) != 0 || num_irq == 0U || num_irq > ARM_M_EXC_NUM - ARM_M_EXC_IRQ0 ||
		    athrill_device_target_node_u32(device_tree_runtime, "arm,v7m-nvic",
				"arm,num-irq-priority-bits", 0U, &priority_bits) != 0 ||
		    priority_bits == 0U || priority_bits > 8U) {
			printf("DEVICE_TREE_ERROR code=nvic node=arm,v7m-nvic property=athrill,num-irqs "
			       "reason=needs athrill,num-irqs (1..%u) and arm,num-irq-priority-bits (1..8)\n",
			       ARM_M_EXC_NUM - ARM_M_EXC_IRQ0);
			exit(1);
		}
		stm32_cpu_configure_nvic(num_irq, priority_bits);
	}
	if (!destroy_registered) {
		if (atexit(stm32_device_tree_destroy) != 0) {
			printf("ERROR: can not register device tree cleanup\n");
			exit(1);
		}
		destroy_registered = TRUE;
	}
	return TRUE;
}

void stm32_device_tree_tick(DeviceClockType *clock)
{
	AthrillDeviceTickResult result;
	if (device_tree_runtime == NULL) {
		return;
	}
	if (athrill_device_tick(device_tree_runtime, clock->clock, &result) != 0) {
		printf("ERROR: device tree tick failed\n");
		exit(1);
	}
	if (result.next_event_interval < clock->min_intr_interval)
		clock->min_intr_interval =
		    result.next_event_interval;
	if (!result.can_skip) {
		clock->can_skip_clock = FALSE;
	}
}

void stm32_device_tree_destroy(void)
{
	if (device_tree_runtime != NULL) {
		athrill_device_destroy(device_tree_runtime);
		device_tree_runtime = NULL;
	}
}

std_bool stm32_device_tree_console_input(uint8 data)
{
	return (device_tree_runtime != NULL &&
		athrill_device_console_input(device_tree_runtime, 0U, data) == 0) ? TRUE : FALSE;
}

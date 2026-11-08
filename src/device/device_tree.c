/*
 * athrill-device target adapter for the STM32 target.
 *
 * Builds the Devicetree runtime (athrill_device_build) and hands it the full
 * AthrillDeviceTargetOps table:
 *   REQUIRED operations   implemented in target_host.c (stm32_host_*)
 *   TO BE REMOVED bridges implemented here: they connect Devicetree nodes to
 *                         models still in the target (SPI, UART, I2C, DMA,
 *                         GPIO lines, sensor source) and go when those models
 *                         move to athrill-device (hakoniwalab/athrill-device#13)
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
#include "sensor/sensor_source.h"
#include "std_errno.h"
#include "target_cpu.h"
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

static sint32 target_model_adapter_connect(void *context,
	const AthrillDeviceNode *node, const char *adapter_name)
{
	(void)context;
	if (node == NULL || node->matched_compatible == NULL || adapter_name == NULL) {
		return -1;
	}
	/*
	 * Each exact DT model delegates to the existing C model independently.
	 * Controllers are reset before the graph is built, so their adapters only
	 * assert that the matching legacy model is already active.  Leaf adapters
	 * attach one chip each.  Shared board tick state is reset before either the
	 * DT graph or the no-DT board_attach path is selected.
	 */
	if ((strcmp(adapter_name, "stm32f7-soc") == 0 &&
	     (strcmp(node->matched_compatible, "st,stm32f765") == 0 ||
	      strcmp(node->matched_compatible, "st,stm32f7") == 0)) ||
	    (strcmp(adapter_name, "stm32f7-crc") == 0 &&
	     strcmp(node->matched_compatible, "zephyr,crc") == 0) ||
	    (strcmp(adapter_name, "stm32h7-soc") == 0 &&
	     (strcmp(node->matched_compatible, "st,stm32h743") == 0 ||
	      strcmp(node->matched_compatible, "st,stm32h7") == 0)) ||
	    (strcmp(adapter_name, "bench-sensor-source") == 0 &&
	     strcmp(node->matched_compatible, "athrill,bench-sensor-source") == 0)) {
		return 0;
	}
	return -1;
}

static sint32 target_sensor_sample(void *context,
	AthrillDeviceSensorSample *sample)
{
	SensorSampleType source;
	(void)context;
	if (sample == NULL) {
		return -1;
	}
	sensor_source_sample(&source);
	memcpy(sample->acceleration, source.acc, sizeof(source.acc));
	memcpy(sample->angular_velocity, source.gyro, sizeof(source.gyro));
	memcpy(sample->magnetic_field, source.mag, sizeof(source.mag));
	sample->pressure_hpa = source.abs_pressure;
	sample->temperature_c = source.temperature;
	return 0;
}

static sint32 target_gps_sample(void *context, AthrillDeviceGpsSample *sample)
{
	GpsSampleType source;
	(void)context;
	if (sample == NULL || !sensor_source_gps(&source)) {
		return -1;
	}
	sample->latitude_deg_e7 = source.lat;
	sample->longitude_deg_e7 = source.lon;
	sample->altitude_mm = source.alt;
	sample->horizontal_accuracy_cm = source.eph;
	sample->vertical_accuracy_cm = source.epv;
	sample->speed_cm_s = source.vel;
	sample->velocity_north_cm_s = source.vn;
	sample->velocity_east_cm_s = source.ve;
	sample->velocity_down_cm_s = source.vd;
	sample->course_cdeg = source.cog;
	sample->fix_type = source.fix_type;
	sample->satellites = source.satellites;
	sample->time_usec = source.time_usec;
	return 0;
}

static sint32 target_actuator_output(void *context, const float32 *values,
	uint32 count)
{
	(void)context;
	if (values == NULL) {
		return -1;
	}
	sensor_source_actuators(values, count);
	return 0;
}

static const AthrillDeviceTargetOps target_ops = {
	ATHRILL_DEVICE_ABI_MAJOR, ATHRILL_DEVICE_ABI_MINOR, sizeof(AthrillDeviceTargetOps),
	/* REQUIRED operations: target_host.c */
	stm32_host_mmio_register, stm32_host_mmio_unregister, stm32_host_irq_resolve, stm32_host_irq_set,
	stm32_host_irq_is_active, stm32_host_time_now, stm32_host_time_frequency_hz,
	stm32_host_clock_frequency_hz, stm32_host_memory_read, stm32_host_memory_write,
	/* TO BE REMOVED bridge operations: this file */
	NULL, NULL, NULL,	/* spi_bind, spi_set_chip_select, spi_exchange_byte: the SPI controllers are athrill-device models */
	NULL, NULL,	/* i2c_bind, i2c_transfer: the I2C controller is an athrill-device model */
	NULL, NULL, NULL,	/* uart_bind, uart_peripheral, uart_controller: the UARTs are athrill-device models */
	NULL, NULL, NULL, NULL,	/* gpio_resolve, gpio_read, gpio_drive, gpio_subscribe: the runtime's GPIO lines (ABI 1.16) */
	NULL, NULL,	/* dma_resolve, dma_request: the DMA is an athrill-device model on request lines */
	target_model_adapter_connect, NULL /* spi_bind_node */, stm32_host_value,
	target_sensor_sample, NULL /* i2c_bind_node */, NULL /* uart_bind_nodes */,
	NULL /* uart_send_from_node */, target_gps_sample, target_actuator_output,
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

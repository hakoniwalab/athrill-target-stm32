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
#include "memory/sdcard.h"
#include "peripheral/stm32h7_sdmmc.h"
#include "bus.h"
#include "cpuemu_ops.h"
#include "device.h"
#include "mpu.h"
#include "peripheral/stm32_sdmmc.h"
#include "peripheral/stm32_uart.h"
#include "sensor/sensor_source.h"
#include "std_errno.h"
#include "target_cpu.h"
#include "target_host.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_UART_ENDPOINTS 16U
#define MAX_GPIO_LINES 192U
#define MAX_GPIO_SUBSCRIBERS 384U
#define MAX_DMA_CHANNELS 64U
#define MAX_PLUGINS 32U
#define MAX_HOST_VALUES 32U
#define MAX_PLUGIN_MAPPING 1024U

typedef struct {
	const AthrillDeviceUartPeripheralOps *ops;
	void *instance;
	AthrillDeviceNodeId controller;
	AthrillDeviceNodeId peripheral;
	uint32 number;
	std_bool active;
} UartEndpoint;

typedef struct {
	uint64 handle;
	uint32 port;
	uint32 pin;
	std_bool level;
	std_bool stm32_pin;
	std_bool used;
} GpioLine;

typedef struct {
	uint64 handle;
	AthrillDeviceGpioEdge callback;
	void *instance;
	std_bool used;
} GpioSubscriber;

typedef struct {
	uint64 handle;
	uint32 index;
	const char *name;
	std_bool asserted;
	std_bool used;
} DmaChannel;

typedef struct {
	UartEndpoint uart[MAX_UART_ENDPOINTS];
	GpioLine gpio[MAX_GPIO_LINES];
	GpioSubscriber gpio_subscribers[MAX_GPIO_SUBSCRIBERS];
	DmaChannel dma[MAX_DMA_CHANNELS];
	uint32 gpio_outputs[11];
} Stm32DeviceTreeTarget;

static Stm32DeviceTreeTarget target_context;
static SdcardType sdcard = { .fd = -1 };
static AthrillDeviceRuntime *device_tree_runtime;
static std_bool destroy_registered;

static char plugin_mappings[MAX_PLUGINS][MAX_PLUGIN_MAPPING];

static sint32 node_path_address(const char *path, uint32 *address)
{
	const char *at;
	char *end;
	unsigned long value;
	if (path == NULL || (at = strrchr(path, '@')) == NULL || at[1] == 0) {
		return -1;
	}
	value = strtoul(at + 1, &end, 16);
	if (*end != 0 || value > 0xffffffffUL) {
		return -1;
	}
	*address = (uint32)value;
	return 0;
}

static sint32 target_uart_bind(void *context, AthrillDeviceNodeId controller,
                               const AthrillDeviceUartPeripheralOps *ops, void *instance, AthrillDeviceUartEndpoint *endpoint)
{
	Stm32DeviceTreeTarget *target = context;
	uint32 i;
	for (i = 0U; i < MAX_UART_ENDPOINTS; i++)
		if (!target->uart[i].active) {
			break;
		}
	if (i == MAX_UART_ENDPOINTS || ops == NULL) {
		return -1;
	}
	target->uart[i].ops = ops;
	target->uart[i].instance = instance;
	target->uart[i].controller = controller;
	target->uart[i].active = TRUE;
	endpoint->value = i + 1U;
	return 0;
}
static sint32 target_uart_peripheral(void *context, AthrillDeviceUartEndpoint endpoint, uint8 data)
{
	Stm32DeviceTreeTarget *t = context;
	uint64 i = endpoint.value;
	if (i == 0U || i > MAX_UART_ENDPOINTS || !t->uart[i - 1U].active ||
	    t->uart[i - 1U].ops->receive_byte == NULL) {
		return -1;
	}
	return t->uart[i - 1U].ops->receive_byte(t->uart[i - 1U].instance, data);
}
static sint32 target_uart_controller(void *context, AthrillDeviceUartEndpoint endpoint, uint8 data)
{
	(void)context;
	(void)endpoint;
	(void)data;
	return 0;
}

static UartEndpoint *uart_endpoint(Stm32DeviceTreeTarget *target,
	AthrillDeviceUartEndpoint endpoint)
{
	return endpoint.value > 0U && endpoint.value <= MAX_UART_ENDPOINTS &&
		target->uart[endpoint.value - 1U].active ?
		&target->uart[endpoint.value - 1U] : NULL;
}

static void target_uart_bridge_transmit(void *instance, uint8 data)
{
	UartEndpoint *endpoint = instance;
	(void)endpoint->ops->receive_byte(endpoint->instance, data);
}

static const Stm32UartChipOps target_uart_bridge_ops = {
	target_uart_bridge_transmit
};

static sint32 target_uart_bind_nodes(void *context,
	const AthrillDeviceNode *controller,
	const AthrillDeviceNode *peripheral,
	const AthrillDeviceUartPeripheralOps *ops, void *instance,
	AthrillDeviceUartEndpoint *endpoint)
{
	Stm32DeviceTreeTarget *target = context;
	UartEndpoint *bound;
	uint32 address, number;
	if (controller == NULL || peripheral == NULL ||
	    node_path_address(controller->path, &address) != 0) {
		return -1;
	}
	switch (address) {
	case 0x40011000U: number = 1U; break;
	case 0x40004400U: number = 2U; break;
	case 0x40004800U: number = 3U; break;
	case 0x40004c00U: number = 4U; break;
	case 0x40005000U: number = 5U; break;
	case 0x40011400U: number = 6U; break;
	case 0x40007800U: number = 7U; break;
	case 0x40007c00U: number = 8U; break;
	default: return -1;
	}
	if (target_uart_bind(context, controller->id, ops, instance, endpoint) != 0) {
		return -1;
	}
	bound = uart_endpoint(target, *endpoint);
	if (bound == NULL) {
		return -1;
	}
	bound->peripheral = peripheral->id;
	bound->number = number;
	stm32_uart_attach(number, &target_uart_bridge_ops, bound);
	return 0;
}

static sint32 target_uart_send_from_node(void *context,
	AthrillDeviceNodeId peripheral, uint8 data)
{
	Stm32DeviceTreeTarget *target = context;
	uint32 i;
	for (i = 0U; i < MAX_UART_ENDPOINTS; i++) {
		if (target->uart[i].active &&
		    target->uart[i].peripheral == peripheral) {
			return stm32_uart_chip_send(target->uart[i].number, data) ==
				STD_E_OK ? 0 : -1;
		}
	}
	return -1;
}

static uint64 spec_handle(const AthrillDeviceSpecifier *spec)
{
	uint64 value = (uint64)spec->provider << 32;
	uint32 i;
	for (i = 0U; i < spec->cell_count; i++) {
		value ^= ((uint64)spec->cells[i]) << ((i % 2U) * 16U);
	}
	return value != 0U ? value : 1U;
}
static GpioLine *gpio_line(Stm32DeviceTreeTarget *target, uint64 handle, std_bool create)
{
	uint32 i, free_index = MAX_GPIO_LINES;
	for (i = 0U; i < MAX_GPIO_LINES; i++) {
		if (target->gpio[i].used && target->gpio[i].handle == handle) {
			return &target->gpio[i];
		}
		if (!target->gpio[i].used && free_index == MAX_GPIO_LINES) {
			free_index = i;
		}
	}
	if (!create || free_index == MAX_GPIO_LINES) {
		return NULL;
	}
	target->gpio[free_index].used = TRUE;
	target->gpio[free_index].handle = handle;
	return &target->gpio[free_index];
}
static sint32 target_gpio_resolve(void *context, const AthrillDeviceSpecifier *spec,
                                  AthrillDeviceGpioLine *line)
{
	GpioLine *gpio;
	uint32 address;
	if (spec == NULL || spec->cell_count == 0U) {
		return -1;
	}
	/* GPIO flags describe the consumer and are not part of a physical line. */
	if (node_path_address(spec->provider_path, &address) == 0) {
		line->value = ((uint64)address << 16) ^ spec->cells[0];
	} else {
		line->value = ((uint64)spec->provider << 32) ^ spec->cells[0];
	}
	if (line->value == 0U) {
		line->value = 1U;
	}
	gpio = gpio_line(context, line->value, TRUE);
	if (gpio == NULL) {
		return -1;
	}
	if (node_path_address(spec->provider_path, &address) == 0 &&
	    (((address >= 0x40020000U && address <= 0x40022800U &&
	       (address - 0x40020000U) % 0x400U == 0U) ||
	      (address >= 0x58020000U && address <= 0x58022800U &&
	       (address - 0x58020000U) % 0x400U == 0U))) &&
	    spec->cells[0] < 16U) {
		gpio->port = address >= 0x58020000U ?
			(address - 0x58020000U) / 0x400U :
			(address - 0x40020000U) / 0x400U;
		gpio->pin = spec->cells[0];
		gpio->stm32_pin = TRUE;
	}
	return 0;
}
static sint32 target_gpio_read(void *context, AthrillDeviceGpioLine line, std_bool *level)
{
	GpioLine *g = gpio_line(context, line.value, FALSE);
	if (g == NULL || level == NULL) {
		return -1;
	}
	*level = g->level;
	return 0;
}
static sint32 target_gpio_drive(void *context, AthrillDeviceGpioLine line, std_bool level)
{
	Stm32DeviceTreeTarget *target = context;
	GpioLine *g = gpio_line(target, line.value, FALSE);
	uint32 i;
	if (g == NULL) {
		return -1;
	}
	if (g->level == level) {
		return 0;
	}
	g->level = level;
	for (i = 0U; i < MAX_GPIO_SUBSCRIBERS; i++)
		if (target->gpio_subscribers[i].used &&
		    target->gpio_subscribers[i].handle == line.value) {
			target->gpio_subscribers[i].callback(target->gpio_subscribers[i].instance, level);
		}
	return 0;
}

static sint32 target_gpio_outputs(void *context,
	const AthrillDeviceGpioLine *lines, uint32 count, uint32 levels)
{
	Stm32DeviceTreeTarget *target = context;
	GpioLine *gpio[16];
	uint32 next, port, index;
	if (lines == NULL || count == 0U || count > 16U) {
		return -1;
	}
	for (index = 0U; index < count; index++) {
		gpio[index] = gpio_line(target, lines[index].value, FALSE);
		if (gpio[index] == NULL || !gpio[index]->stm32_pin ||
		    gpio[index]->port >= 11U || gpio[index]->pin != index ||
		    (index != 0U && gpio[index]->port != gpio[0]->port)) {
			return -1;
		}
	}
	port = gpio[0]->port;
	next = levels & ((1U << count) - 1U);
	for (index = 0U; index < count; index++) {
		gpio[index]->level = (next & (1U << index)) != 0U;
	}
	target->gpio_outputs[port] = next;
	return 0;
}
static sint32 target_gpio_subscribe(void *context, AthrillDeviceGpioLine line,
                                    AthrillDeviceGpioEdge callback, void *instance)
{
	Stm32DeviceTreeTarget *target = context;
	uint32 i;
	if (gpio_line(target, line.value, FALSE) == NULL || callback == NULL) {
		return -1;
	}
	for (i = 0U; i < MAX_GPIO_SUBSCRIBERS; i++)
		if (!target->gpio_subscribers[i].used) {
			target->gpio_subscribers[i].used = TRUE;
			target->gpio_subscribers[i].handle = line.value;
			target->gpio_subscribers[i].callback = callback;
			target->gpio_subscribers[i].instance = instance;
			return 0;
		}
	return -1;
}

static DmaChannel *dma_channel(Stm32DeviceTreeTarget *target, uint64 handle, std_bool create)
{
	uint32 i, free_index = MAX_DMA_CHANNELS;
	for (i = 0U; i < MAX_DMA_CHANNELS; i++) {
		if (target->dma[i].used && target->dma[i].handle == handle) {
			return &target->dma[i];
		}
		if (!target->dma[i].used && free_index == MAX_DMA_CHANNELS) {
			free_index = i;
		}
	}
	if (!create || free_index == MAX_DMA_CHANNELS) {
		return NULL;
	}
	target->dma[free_index].used = TRUE;
	target->dma[free_index].handle = handle;
	return &target->dma[free_index];
}
/* [ADV-SPI-006] accept the SPI tuple order and tx/rx direction. */
/* [ADV-UART-006] accept the UART tuple order and tx/rx direction. */
static sint32 target_dma_resolve(void *context, const AthrillDeviceSpecifier *spec,
                                 AthrillDeviceDmaChannel *channel)
{
	DmaChannel *resolved;
	if (spec == NULL || spec->name == NULL) {
		return -1;
	}
	channel->value = spec_handle(spec);
	resolved = dma_channel(context, channel->value, TRUE);
	if (resolved == NULL) {
		return -1;
	}
	resolved->index = spec->index;
	resolved->name = spec->name;
	return 0;
}
static sint32 target_dma_request(void *context, AthrillDeviceDmaChannel channel, std_bool asserted)
{
	DmaChannel *d = dma_channel(context, channel.value, FALSE);
	if (d == NULL) {
		return -1;
	}
	d->asserted = asserted;
	return 0;
}

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
	if (strcmp(adapter_name, "sdcard") == 0 &&
	    strcmp(node->matched_compatible, "zephyr,sdmmc-disk") == 0) {
		AthrillDeviceSpecifier spec;
		AthrillDeviceGpioLine line;
		/* The card image (DEVICE_CONFIG_STM32_SDCARD_IMAGE; none: empty slot)
		 * on the SDMMC model of the SoC (moved here from the C-path boards). */
		char *sd_image = NULL;
		if (cpuemu_get_devcfg_string("DEVICE_CONFIG_STM32_SDCARD_IMAGE", &sd_image) != STD_E_OK) {
			sd_image = NULL;
		}
		sdcard_open(&sdcard, sd_image);
		if (!sdcard_present(&sdcard)) {
			return 0;
		}
		if (strstr(node->path, "/sdmmc@48022400/") != NULL) {
			stm32h7_sdmmc_attach(&sdcard);
			return 0;
		}
		stm32_sdmmc_attach(&sdcard);
		/* F7: D0 high (PC8), the card is not busy */
		memset(&spec, 0, sizeof(spec));
		spec.provider_path = "/gpio@40020800";
		spec.provider_compatible = "st,stm32-gpio";
		spec.cell_count = 2U;
		spec.cells[0] = 8U;
		if (target_gpio_resolve(context, &spec, &line) != 0 ||
		    target_gpio_drive(context, line, TRUE) != 0) {
			return -1;
		}
		return 0;
	}
	if ((strcmp(adapter_name, "stm32f7-soc") == 0 &&
	     (strcmp(node->matched_compatible, "st,stm32f765") == 0 ||
	      strcmp(node->matched_compatible, "st,stm32f7") == 0)) ||
	    (strcmp(adapter_name, "stm32-dma") == 0 &&
	     strcmp(node->matched_compatible, "st,stm32-dma-v1") == 0) ||
	    (strcmp(adapter_name, "stm32f7-uart") == 0 &&
	     (strcmp(node->matched_compatible, "st,stm32-usart") == 0 ||
	      strcmp(node->matched_compatible, "st,stm32-uart") == 0)) ||
	    (strcmp(adapter_name, "stm32-sdmmc") == 0 &&
	     strcmp(node->matched_compatible, "st,stm32-sdmmc") == 0) ||
	    (strcmp(adapter_name, "stm32f7-flash") == 0 &&
	     (strcmp(node->matched_compatible, "st,stm32-flash-controller") == 0 ||
	      strcmp(node->matched_compatible, "st,stm32f7-flash-controller") == 0 ||
	      strcmp(node->matched_compatible, "zephyr,flash-controller") == 0)) ||
	    (strcmp(adapter_name, "stm32f7-rtc") == 0 &&
	     strcmp(node->matched_compatible, "st,stm32-rtc") == 0) ||
	    (strcmp(adapter_name, "stm32f7-adc") == 0 &&
	     strcmp(node->matched_compatible, "st,stm32-adc") == 0) ||
	    (strcmp(adapter_name, "stm32f7-rng") == 0 &&
	     (strcmp(node->matched_compatible, "st,stm32-rng") == 0 ||
	      strcmp(node->matched_compatible, "zephyr,entropy") == 0)) ||
	    (strcmp(adapter_name, "stm32f7-otgfs") == 0 &&
	     strcmp(node->matched_compatible, "st,stm32-otgfs") == 0) ||
	    (strcmp(adapter_name, "stm32f7-crc") == 0 &&
	     strcmp(node->matched_compatible, "zephyr,crc") == 0) ||
	    (strcmp(adapter_name, "stm32f7-backup-sram") == 0 &&
	     strcmp(node->matched_compatible, "st,stm32-backup-sram") == 0) ||
	    (strcmp(adapter_name, "stm32h7-soc") == 0 &&
	     (strcmp(node->matched_compatible, "st,stm32h743") == 0 ||
	      strcmp(node->matched_compatible, "st,stm32h7") == 0)) ||
	    (strcmp(adapter_name, "stm32h7-rcc") == 0 &&
	     strcmp(node->matched_compatible, "st,stm32h7-rcc") == 0) ||
	    (strcmp(adapter_name, "stm32h7-pwr") == 0 &&
	     strcmp(node->matched_compatible, "st,stm32h7-pwr") == 0) ||
	    (strcmp(adapter_name, "stm32h7-boot-register") == 0 &&
	     (strcmp(node->matched_compatible, "st,stm32-bdma") == 0 ||
	      strcmp(node->matched_compatible, "st,stm32-watchdog") == 0 ||
	      strcmp(node->matched_compatible, "st,stm32h7-syscfg") == 0 ||
	      strcmp(node->matched_compatible, "st,stm32h7-axi-target") == 0 ||
	      strcmp(node->matched_compatible, "st,stm32h7-dbgmcu") == 0 ||
	      strcmp(node->matched_compatible, "st,stm32h7-system-memory") == 0 ||
	      strcmp(node->matched_compatible, "st,stm32h7-fdcan") == 0)) ||
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
	target_uart_bind, target_uart_peripheral, target_uart_controller, target_gpio_resolve, target_gpio_read,
	target_gpio_drive, target_gpio_subscribe, target_dma_resolve, target_dma_request,
	target_model_adapter_connect, NULL /* spi_bind_node */, stm32_host_value,
	target_sensor_sample, NULL /* i2c_bind_node */, target_uart_bind_nodes,
	target_uart_send_from_node, target_gps_sample, target_actuator_output,
	target_gpio_outputs,
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

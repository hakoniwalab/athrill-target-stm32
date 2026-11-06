/*
 * athrill-device target adapter for the STM32 target.
 * Copyright 2026 Hakoniwa Laboratory
 * SPDX-License-Identifier: MIT
 */
#include "device_tree.h"

#include "athrill_device_if.h"
#include "board/board.h"
#include "bus.h"
#include "cpuemu_ops.h"
#include "device.h"
#include "mpu.h"
#include "peripheral/stm32_gpio.h"
#include "peripheral/stm32_i2c.h"
#include "peripheral/stm32_spi.h"
#include "peripheral/stm32_uart.h"
#include "sensor/sensor_source.h"
#include "std_errno.h"
#include "target_cpu.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_MMIO_ADAPTERS 64U
#define MAX_SPI_ENDPOINTS 32U
#define MAX_I2C_ENDPOINTS 32U
#define MAX_UART_ENDPOINTS 16U
#define MAX_GPIO_LINES 128U
#define MAX_GPIO_SUBSCRIBERS 128U
#define MAX_DMA_CHANNELS 64U
#define MAX_PLUGINS 32U
#define MAX_HOST_VALUES 16U

typedef struct {
	const AthrillDeviceMmioOps *ops;
	void *instance;
	uint32 region_index;
	std_bool active;
} MmioAdapter;

typedef struct {
	const AthrillDeviceSpiPeripheralOps *ops;
	void *instance;
	AthrillDeviceNodeId controller;
	uint32 chip_select;
	std_bool active;
} SpiEndpoint;

typedef struct {
	const AthrillDeviceI2cPeripheralOps *ops;
	void *instance;
	AthrillDeviceNodeId controller;
	uint16 address;
	uint8 pending[256];
	uint32 pending_count;
	std_bool reading;
	std_bool failed;
	std_bool active;
} I2cEndpoint;

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
	std_bool asserted;
	std_bool used;
} DmaChannel;

typedef struct {
	MmioAdapter mmio[MAX_MMIO_ADAPTERS];
	SpiEndpoint spi[MAX_SPI_ENDPOINTS];
	I2cEndpoint i2c[MAX_I2C_ENDPOINTS];
	UartEndpoint uart[MAX_UART_ENDPOINTS];
	GpioLine gpio[MAX_GPIO_LINES];
	GpioSubscriber gpio_subscribers[MAX_GPIO_SUBSCRIBERS];
	DmaChannel dma[MAX_DMA_CHANNELS];
} Stm32DeviceTreeTarget;

static Stm32DeviceTreeTarget target_context;
static AthrillDeviceRuntime *device_tree_runtime;

static MmioAdapter *mmio_of(MpuAddressRegionType *region)
{
	return (MmioAdapter *)region->data;
}

static Std_ReturnType mmio_get8(MpuAddressRegionType *region, CoreIdType core,
                                AthrillAddrType address, uint8 *data)
{
	uint64 value;
	MmioAdapter *adapter = mmio_of(region);
	(void)core;
	if (!adapter->active ||
	    adapter->ops->read(adapter->instance, address, 1U, &value) != 0) {
		return STD_E_INVALID;
	}
	*data = (uint8)value;
	return STD_E_OK;
}
static Std_ReturnType mmio_get16(MpuAddressRegionType *region, CoreIdType core,
                                 AthrillAddrType address, uint16 *data)
{
	uint64 value;
	MmioAdapter *adapter = mmio_of(region);
	(void)core;
	if (!adapter->active ||
	    adapter->ops->read(adapter->instance, address, 2U, &value) != 0) {
		return STD_E_INVALID;
	}
	*data = (uint16)value;
	return STD_E_OK;
}
static Std_ReturnType mmio_get32(MpuAddressRegionType *region, CoreIdType core,
                                 AthrillAddrType address, uint32 *data)
{
	uint64 value;
	MmioAdapter *adapter = mmio_of(region);
	(void)core;
	if (!adapter->active ||
	    adapter->ops->read(adapter->instance, address, 4U, &value) != 0) {
		return STD_E_INVALID;
	}
	*data = (uint32)value;
	return STD_E_OK;
}
static Std_ReturnType mmio_put8(MpuAddressRegionType *region, CoreIdType core,
                                AthrillAddrType address, uint8 data)
{
	MmioAdapter *adapter = mmio_of(region);
	(void)core;
	return (!adapter->active ||
	        adapter->ops->write(adapter->instance, address, 1U, data) != 0) ? STD_E_INVALID : STD_E_OK;
}
static Std_ReturnType mmio_put16(MpuAddressRegionType *region, CoreIdType core,
                                 AthrillAddrType address, uint16 data)
{
	MmioAdapter *adapter = mmio_of(region);
	(void)core;
	return (!adapter->active ||
	        adapter->ops->write(adapter->instance, address, 2U, data) != 0) ? STD_E_INVALID : STD_E_OK;
}
static Std_ReturnType mmio_put32(MpuAddressRegionType *region, CoreIdType core,
                                 AthrillAddrType address, uint32 data)
{
	MmioAdapter *adapter = mmio_of(region);
	(void)core;
	return (!adapter->active ||
	        adapter->ops->write(adapter->instance, address, 4U, data) != 0) ? STD_E_INVALID : STD_E_OK;
}
static Std_ReturnType mmio_pointer(MpuAddressRegionType *region, CoreIdType core,
                                   AthrillAddrType address, uint8 **data)
{
	(void)region;
	(void)core;
	(void)address;
	(void)data;
	return STD_E_INVALID;
}
static MpuAddressRegionOperationType mmio_region_ops = {
	mmio_get8, mmio_get16, mmio_get32, mmio_put8, mmio_put16, mmio_put32, mmio_pointer
};

static sint32 target_mmio_register(void *context, uint64 address, uint64 size,
                                   const AthrillDeviceMmioOps *ops, void *instance, AthrillDeviceMmioToken *token)
{
	Stm32DeviceTreeTarget *target = context;
	uint32 i;
	MpuAddressRegionType *maps;
	if (address > 0xffffffffULL || size == 0U || size > 0xffffffffULL ||
	    address + size > 0x100000000ULL) {
		return -1;
	}
	for (i = 0U; i < MAX_MMIO_ADAPTERS; i++)
		if (!target->mmio[i].active) {
			break;
		}
	if (i == MAX_MMIO_ADAPTERS) {
		return -1;
	}
	target->mmio[i].ops = ops;
	target->mmio[i].instance = instance;
	target->mmio[i].active = TRUE;
	maps = realloc(mpu_address_map.dynamic_map, sizeof(*maps) * (mpu_address_map.dynamic_map_num + 1U));
	if (maps == NULL) {
		target->mmio[i].active = FALSE;
		return -1;
	}
	mpu_address_map.dynamic_map = maps;
	target->mmio[i].region_index = mpu_address_map.dynamic_map_num;
	maps[target->mmio[i].region_index].type = DEVICE;
	maps[target->mmio[i].region_index].is_malloc = FALSE;
	maps[target->mmio[i].region_index].permission = MPU_ADDRESS_REGION_PERM_ALL;
	maps[target->mmio[i].region_index].start = (AthrillAddrType)address;
	maps[target->mmio[i].region_index].size = (AthrillAddrType)size;
	maps[target->mmio[i].region_index].mask = MPU_ADDRESS_REGION_MASK_ALL;
	maps[target->mmio[i].region_index].data = (uint8 *)&target->mmio[i];
	maps[target->mmio[i].region_index].ops = &mmio_region_ops;
	mpu_address_map.dynamic_map_num++;
	token->value = (uint64)i + 1U;
	return 0;
}

static void target_mmio_unregister(void *context, AthrillDeviceMmioToken token)
{
	Stm32DeviceTreeTarget *target = context;
	uint64 i = token.value;
	if (i == 0U || i > MAX_MMIO_ADAPTERS) {
		return;
	}
	i--;
	if (target->mmio[i].active) {
		target->mmio[i].active = FALSE;
		if (target->mmio[i].region_index < mpu_address_map.dynamic_map_num) {
			mpu_address_map.dynamic_map[target->mmio[i].region_index].size = 0U;
		}
	}
}

static sint32 target_irq_resolve(void *context, const AthrillDeviceSpecifier *spec,
                                 AthrillDeviceIrqLine *line)
{
	(void)context;
	if (spec == NULL || spec->cell_count == 0U) {
		return -1;
	}
	line->value = spec->cells[0];
	return line->value < virtual_cpu.cores[0].core.m.num_irq ? 0 : -1;
}
static sint32 target_irq_set(void *context, AthrillDeviceIrqLine line, std_bool asserted)
{
	TargetCoreType *core = &virtual_cpu.cores[0].core;
	(void)context;
	if (line.value >= core->m.num_irq) {
		return -1;
	}
	if (asserted) {
		return intc_raise_intr((uint32)line.value);
	}
	if (core->m.active[ARM_M_EXC_IRQ0 + (uint32)line.value] == 0U)
		core->m.pending[ARM_M_EXC_IRQ0 +
		                (uint32)line.value] = 0U;
	return 0;
}
static std_bool target_irq_active(void *context, AthrillDeviceIrqLine line)
{
	(void)context;
	return line.value <= 0xffffffffU ? intc_irq_active((uint32)line.value) : FALSE;
}
static uint64 target_time_now(void *context)
{
	(void)context;
	return cpuemu_get_clock();
}
static uint64 target_time_frequency(void *context)
{
	uint32 mhz = 216U;
	(void)context;
	(void)cpuemu_get_devcfg_value("DEVICE_CPU_FREQ", &mhz);
	return (uint64)mhz * 1000000U;
}
static sint32 target_clock_frequency(void *context, AthrillDeviceNodeId node,
                                     const char *clock_name, uint64 *frequency)
{
	(void)context;
	(void)node;
	(void)clock_name;
	*frequency = target_time_frequency(NULL);
	return 0;
}

static sint32 target_memory_read(void *context, uint64 address, void *data, uint32 size)
{
	uint32 i;
	(void)context;
	if (address > 0xffffffffULL || address + size > 0x100000000ULL) {
		return -1;
	}
	for (i = 0U; i < size;
	     i++)
		if (bus_get_data8(0U, (AthrillAddrType)(address + i),
		                  &((uint8 *)data)[i]) != STD_E_OK) {
			return -1;
		}
	return 0;
}
static sint32 target_memory_write(void *context, uint64 address, const void *data, uint32 size)
{
	uint32 i;
	(void)context;
	if (address > 0xffffffffULL || address + size > 0x100000000ULL) {
		return -1;
	}
	for (i = 0U; i < size;
	     i++)
		if (bus_put_data8(0U, (AthrillAddrType)(address + i),
		                  ((const uint8 *)data)[i]) != STD_E_OK) {
			return -1;
		}
	return 0;
}

static sint32 target_spi_bind(void *context, AthrillDeviceNodeId controller, uint32 chip_select,
                              const AthrillDeviceSpecifier *gpio, const AthrillDeviceSpiPeripheralOps *ops, void *instance,
                              AthrillDeviceSpiEndpoint *endpoint)
{
	Stm32DeviceTreeTarget *target = context;
	uint32 i;
	(void)gpio;
	for (i = 0U; i < MAX_SPI_ENDPOINTS; i++)
		if (!target->spi[i].active) {
			break;
		}
	if (i == MAX_SPI_ENDPOINTS || ops == NULL) {
		return -1;
	}
	target->spi[i].ops = ops;
	target->spi[i].instance = instance;
	target->spi[i].controller = controller;
	target->spi[i].chip_select = chip_select;
	target->spi[i].active = TRUE;
	endpoint->value = i + 1U;
	return 0;
}
static SpiEndpoint *spi_endpoint(Stm32DeviceTreeTarget *target, AthrillDeviceSpiEndpoint endpoint)
{
	return endpoint.value > 0U && endpoint.value <= MAX_SPI_ENDPOINTS &&
	       target->spi[endpoint.value - 1U].active ? &target->spi[endpoint.value - 1U] : NULL;
}

static void target_spi_bridge_select(void *instance, std_bool selected)
{
	SpiEndpoint *endpoint = instance;
	endpoint->ops->set_chip_select(endpoint->instance, selected);
}

static uint8 target_spi_bridge_exchange(void *instance, uint8 mosi)
{
	SpiEndpoint *endpoint = instance;
	return endpoint->ops->exchange_byte(endpoint->instance, mosi);
}

static const Stm32SpiChipOps target_spi_bridge_ops = {
	target_spi_bridge_select, target_spi_bridge_exchange
};

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

static sint32 target_spi_bind_node(void *context,
	const AthrillDeviceNode *controller, uint32 chip_select,
	const AthrillDeviceSpecifier *gpio,
	const AthrillDeviceSpiPeripheralOps *ops, void *instance,
	AthrillDeviceSpiEndpoint *endpoint)
{
	Stm32DeviceTreeTarget *target = context;
	SpiEndpoint *bound;
	uint32 controller_address, gpio_address, bus, port, pin;
	if (controller == NULL || gpio == NULL || gpio->cell_count < 1U ||
	    node_path_address(controller->path, &controller_address) != 0 ||
	    node_path_address(gpio->provider_path, &gpio_address) != 0 ||
	    gpio_address < 0x40020000U || gpio_address > 0x40022800U ||
	    (gpio_address - 0x40020000U) % 0x400U != 0U) {
		return -1;
	}
	switch (controller_address) {
	case 0x40013000U: bus = 1U; break;
	case 0x40003800U: bus = 2U; break;
	case 0x40003c00U: bus = 3U; break;
	case 0x40013400U: bus = 4U; break;
	case 0x40015000U: bus = 5U; break;
	case 0x40015400U: bus = 6U; break;
	default: return -1;
	}
	port = (gpio_address - 0x40020000U) / 0x400U;
	pin = gpio->cells[0];
	if (port > STM32_GPIO_PORT('K') || pin >= 16U ||
	    target_spi_bind(context, controller->id, chip_select, gpio, ops,
		instance, endpoint) != 0) {
		return -1;
	}
	bound = spi_endpoint(target, *endpoint);
	if (bound == NULL) {
		return -1;
	}
	stm32_spi_attach(bus, port, pin, &target_spi_bridge_ops, bound);
	return 0;
}
static sint32 target_spi_select(void *context, AthrillDeviceSpiEndpoint endpoint, std_bool asserted)
{
	SpiEndpoint *e = spi_endpoint(context, endpoint);
	if (e == NULL || e->ops->set_chip_select == NULL) {
		return -1;
	}
	e->ops->set_chip_select(e->instance, asserted);
	return 0;
}
static sint32 target_spi_exchange(void *context, AthrillDeviceSpiEndpoint endpoint, uint8 mosi,
                                  uint8 *miso)
{
	SpiEndpoint *e = spi_endpoint(context, endpoint);
	if (e == NULL || e->ops->exchange_byte == NULL || miso == NULL) {
		return -1;
	}
	*miso = e->ops->exchange_byte(e->instance, mosi);
	return 0;
}

static sint32 target_i2c_bind(void *context, AthrillDeviceNodeId controller, uint16 address,
                              const AthrillDeviceI2cPeripheralOps *ops, void *instance, AthrillDeviceI2cEndpoint *endpoint)
{
	Stm32DeviceTreeTarget *target = context;
	uint32 i, j;
	if (address > 0x7fU || ops == NULL) {
		return -1;
	}
	for (j = 0U; j < MAX_I2C_ENDPOINTS; j++)
		if (target->i2c[j].active &&
		    target->i2c[j].controller == controller && target->i2c[j].address == address) {
			return -1;
		}
	for (i = 0U; i < MAX_I2C_ENDPOINTS; i++)
		if (!target->i2c[i].active) {
			break;
		}
	if (i == MAX_I2C_ENDPOINTS) {
		return -1;
	}
	target->i2c[i].ops = ops;
	target->i2c[i].instance = instance;
	target->i2c[i].controller = controller;
	target->i2c[i].address = address;
	target->i2c[i].active = TRUE;
	endpoint->value = i + 1U;
	return 0;
}
static sint32 target_i2c_transfer(void *context, AthrillDeviceI2cEndpoint endpoint,
                                  AthrillDeviceI2cMessage *messages, uint32 count)
{
	Stm32DeviceTreeTarget *t = context;
	uint64 i = endpoint.value;
	if (i == 0U || i > MAX_I2C_ENDPOINTS || !t->i2c[i - 1U].active) {
		return -1;
	}
	return t->i2c[i - 1U].ops->transfer(t->i2c[i - 1U].instance, messages, count);
}

static I2cEndpoint *i2c_endpoint(Stm32DeviceTreeTarget *target,
	AthrillDeviceI2cEndpoint endpoint)
{
	return endpoint.value > 0U && endpoint.value <= MAX_I2C_ENDPOINTS &&
		target->i2c[endpoint.value - 1U].active ?
		&target->i2c[endpoint.value - 1U] : NULL;
}

static sint32 target_i2c_bridge_flush(I2cEndpoint *endpoint,
	std_bool stop)
{
	AthrillDeviceI2cMessage message;
	if (endpoint->pending_count == 0U) {
		return 0;
	}
	message.data = endpoint->pending;
	message.size = endpoint->pending_count;
	message.read = FALSE;
	message.stop = stop;
	endpoint->pending_count = 0U;
	return endpoint->ops->transfer(endpoint->instance, &message, 1U);
}

static std_bool target_i2c_bridge_start(void *instance, std_bool read)
{
	I2cEndpoint *endpoint = instance;
	if (endpoint->ops->start != NULL) {
		return endpoint->ops->start(endpoint->instance, read);
	}
	if (target_i2c_bridge_flush(endpoint, FALSE) != 0) {
		endpoint->failed = TRUE;
		return FALSE;
	}
	endpoint->reading = read;
	return TRUE;
}

static void target_i2c_bridge_write(void *instance, uint8 data)
{
	I2cEndpoint *endpoint = instance;
	if (endpoint->ops->write_byte != NULL) {
		endpoint->ops->write_byte(endpoint->instance, data);
		return;
	}
	if (endpoint->pending_count >= sizeof(endpoint->pending)) {
		endpoint->failed = TRUE;
		return;
	}
	endpoint->pending[endpoint->pending_count++] = data;
}

static uint8 target_i2c_bridge_read(void *instance)
{
	I2cEndpoint *endpoint = instance;
	AthrillDeviceI2cMessage message;
	uint8 data = 0xffU;
	if (endpoint->ops->read_byte != NULL) {
		return endpoint->ops->read_byte(endpoint->instance);
	}
	message.data = &data;
	message.size = 1U;
	message.read = TRUE;
	message.stop = FALSE;
	if (endpoint->ops->transfer(endpoint->instance, &message, 1U) != 0) {
		endpoint->failed = TRUE;
	}
	return data;
}

static void target_i2c_bridge_stop(void *instance)
{
	I2cEndpoint *endpoint = instance;
	AthrillDeviceI2cMessage message;
	if (endpoint->ops->stop != NULL) {
		endpoint->ops->stop(endpoint->instance);
		return;
	}
	if (endpoint->reading) {
		message.data = NULL;
		message.size = 0U;
		message.read = TRUE;
		message.stop = TRUE;
		if (endpoint->ops->transfer(endpoint->instance, &message, 1U) != 0) {
			endpoint->failed = TRUE;
		}
	} else if (target_i2c_bridge_flush(endpoint, TRUE) != 0) {
		endpoint->failed = TRUE;
	}
	endpoint->reading = FALSE;
}

static const Stm32I2cChipOps target_i2c_bridge_ops = {
	target_i2c_bridge_start, target_i2c_bridge_write,
	target_i2c_bridge_read, target_i2c_bridge_stop
};

static sint32 target_i2c_bind_node(void *context,
	const AthrillDeviceNode *controller, uint16 address,
	const AthrillDeviceI2cPeripheralOps *ops, void *instance,
	AthrillDeviceI2cEndpoint *endpoint)
{
	Stm32DeviceTreeTarget *target = context;
	I2cEndpoint *bound;
	uint32 controller_address, bus;
	if (controller == NULL ||
	    node_path_address(controller->path, &controller_address) != 0) {
		return -1;
	}
	switch (controller_address) {
	case 0x40005400U: bus = 1U; break;
	case 0x40005800U: bus = 2U; break;
	case 0x40005c00U: bus = 3U; break;
	case 0x40006000U:
	case 0x58001c00U: bus = 4U; break;
	default: return -1;
	}
	if (target_i2c_bind(context, controller->id, address, ops, instance,
		endpoint) != 0) {
		return -1;
	}
	bound = i2c_endpoint(target, *endpoint);
	if (bound == NULL) {
		return -1;
	}
	stm32_i2c_attach(bus, address, &target_i2c_bridge_ops, bound);
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
	line->value = spec_handle(spec);
	gpio = gpio_line(context, line->value, TRUE);
	if (gpio == NULL) {
		return -1;
	}
	if (node_path_address(spec->provider_path, &address) == 0 &&
	    address >= 0x40020000U && address <= 0x40022800U &&
	    (address - 0x40020000U) % 0x400U == 0U &&
	    spec->cells[0] < 16U) {
		gpio->port = (address - 0x40020000U) / 0x400U;
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
	if (g->stm32_pin) {
		stm32_gpio_drive(g->port, g->pin, level);
	}
	for (i = 0U; i < MAX_GPIO_SUBSCRIBERS; i++)
		if (target->gpio_subscribers[i].used &&
		    target->gpio_subscribers[i].handle == line.value) {
			target->gpio_subscribers[i].callback(target->gpio_subscribers[i].instance, level);
		}
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
static sint32 target_dma_resolve(void *context, const AthrillDeviceSpecifier *spec,
                                 AthrillDeviceDmaChannel *channel)
{
	if (spec == NULL) {
		return -1;
	}
	channel->value = spec_handle(spec);
	return dma_channel(context, channel->value, TRUE) != NULL ? 0 : -1;
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
	 * attach one chip each.  The no-DT board_attach path remains unchanged.
	 */
	if (strcmp(adapter_name, "pixhawk4-board") == 0 &&
	    strcmp(node->matched_compatible, "holybro,pixhawk4") == 0) {
		board_fmu_v5_begin();
		return 0;
	}
	if (strcmp(adapter_name, "pwm-actuator-map") == 0 &&
	    strcmp(node->matched_compatible, "athrill,pwm-actuator-map") == 0) {
		board_fmu_v5_pwm_attach();
		return 0;
	}
	if (strcmp(adapter_name, "sdcard") == 0 &&
	    strcmp(node->matched_compatible, "zephyr,sdmmc-disk") == 0) {
		board_fmu_v5_sdcard_attach();
		return 0;
	}
	if ((strcmp(adapter_name, "stm32f7-soc") == 0 &&
	     (strcmp(node->matched_compatible, "st,stm32f765") == 0 ||
	      strcmp(node->matched_compatible, "st,stm32f7") == 0)) ||
	    (strcmp(adapter_name, "stm32f7-gpio") == 0 &&
	     strcmp(node->matched_compatible, "st,stm32-gpio") == 0) ||
	    (strcmp(adapter_name, "stm32f7-exti") == 0 &&
	     strcmp(node->matched_compatible, "st,stm32-exti") == 0) ||
	    (strcmp(adapter_name, "stm32f7-dma") == 0 &&
	     strcmp(node->matched_compatible, "st,stm32-dma-v1") == 0) ||
	    (strcmp(adapter_name, "stm32f7-spi") == 0 &&
	     (strcmp(node->matched_compatible, "st,stm32-spi-fifo") == 0 ||
	      strcmp(node->matched_compatible, "st,stm32-spi") == 0)) ||
	    (strcmp(adapter_name, "stm32f7-i2c") == 0 &&
	     strcmp(node->matched_compatible, "st,stm32-i2c-v2") == 0) ||
	    (strcmp(adapter_name, "stm32f7-timer") == 0 &&
	     strcmp(node->matched_compatible, "st,stm32-timers") == 0) ||
	    (strcmp(adapter_name, "stm32f7-pwm") == 0 &&
	     strcmp(node->matched_compatible, "st,stm32-pwm") == 0) ||
	    (strcmp(adapter_name, "stm32f7-uart") == 0 &&
	     (strcmp(node->matched_compatible, "st,stm32-usart") == 0 ||
	      strcmp(node->matched_compatible, "st,stm32-uart") == 0)) ||
	    (strcmp(adapter_name, "stm32f7-sdmmc") == 0 &&
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
	    (strcmp(adapter_name, "bench-sensor-source") == 0 &&
	     strcmp(node->matched_compatible, "athrill,bench-sensor-source") == 0)) {
		return 0;
	}
	return -1;
}

static Std_ReturnType config_string_silent(const char *key, char **value);

static sint32 target_host_value(void *context, const char *key,
	const char **value)
{
	char *configured = NULL;
	(void)context;
	if (key == NULL || value == NULL ||
	    config_string_silent(key, &configured) != STD_E_OK) {
		return -1;
	}
	*value = configured;
	return 0;
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

static const AthrillDeviceTargetOps target_ops = {
	ATHRILL_DEVICE_ABI_MAJOR, ATHRILL_DEVICE_ABI_MINOR, sizeof(AthrillDeviceTargetOps),
	target_mmio_register, target_mmio_unregister, target_irq_resolve, target_irq_set, target_irq_active,
	target_time_now, target_time_frequency, target_clock_frequency, target_memory_read, target_memory_write,
	target_spi_bind, target_spi_select, target_spi_exchange, target_i2c_bind, target_i2c_transfer,
	target_uart_bind, target_uart_peripheral, target_uart_controller, target_gpio_resolve, target_gpio_read,
	target_gpio_drive, target_gpio_subscribe, target_dma_resolve, target_dma_request,
	target_model_adapter_connect, target_spi_bind_node, target_host_value,
	target_sensor_sample, target_i2c_bind_node, target_uart_bind_nodes,
	target_uart_send_from_node, target_gps_sample
};

static Std_ReturnType config_string_silent(const char *key, char **value)
{
	int saved_stdout, null_fd;
	Std_ReturnType result;
	(void)fflush(stdout);
	saved_stdout = dup(STDOUT_FILENO);
	null_fd = open("/dev/null", O_WRONLY);
	if (saved_stdout < 0 || null_fd < 0 || dup2(null_fd, STDOUT_FILENO) < 0) {
		if (saved_stdout >= 0) {
			close(saved_stdout);
		}
		if (null_fd >= 0) {
			close(null_fd);
		}
		return cpuemu_get_devcfg_string(key, value);
	}
	result = cpuemu_get_devcfg_string(key, value);
	(void)fflush(stdout);
	(void)dup2(saved_stdout, STDOUT_FILENO);
	(void)close(saved_stdout);
	(void)close(null_fd);
	return result;
}

static uint32 collect_host_values(AthrillDeviceHostValue values[MAX_HOST_VALUES])
{
	static const char *const keys[] = {
		"DEVICE_CONFIG_STM32_FRAM_FILE", "DEVICE_CONFIG_STM32_SDCARD_IMAGE",
		"DEVICE_CONFIG_STM32_UART_RX_FILE", "DEVICE_CONFIG_STM32_UART_TX_FILE",
		"DEVICE_CONFIG_STM32_ACTUATOR_FILE", "DEVICE_CONFIG_STM32_SENSOR_BENCH",
		"DEVICE_CONFIG_STM32_SENSOR_DEVICE", "DEVICE_CONFIG_STM32_UART2_TCP_PORT"
	};
	uint32 count = 0U, i;
	char *value;
	for (i = 0U; i < sizeof(keys) / sizeof(keys[0]) && count < MAX_HOST_VALUES; i++) {
		value = NULL;
		if (config_string_silent(keys[i], &value) == STD_E_OK) {
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
		if (config_string_silent(key, &mapping) != STD_E_OK) {
			break;
		}
		equals = strchr(mapping, '=');
		if (equals == NULL || equals == mapping || equals[1] == 0) {
			return -1;
		}
		*equals = 0;
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
	int plugin_count;
	stm32_device_tree_destroy();
	if (cpuemu_get_devcfg_string("DEVICE_CONFIG_DEVICE_TREE", &path) != STD_E_OK) {
		return FALSE;
	}
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

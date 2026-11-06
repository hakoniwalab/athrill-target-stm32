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
	std_bool active;
} I2cEndpoint;

typedef struct {
	const AthrillDeviceUartPeripheralOps *ops;
	void *instance;
	AthrillDeviceNodeId controller;
	std_bool active;
} UartEndpoint;

typedef struct {
	uint64 handle;
	std_bool level;
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
	(void)context;
	return (uint64)cpuemu_get_cpu_frequency() * 1000000U;
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
	if (spec == NULL || spec->cell_count == 0U) {
		return -1;
	}
	line->value = spec_handle(spec);
	return gpio_line(context, line->value, TRUE) != NULL ? 0 : -1;
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
	if (node == NULL || node->matched_compatible == NULL ||
	    adapter_name == NULL || strcmp(adapter_name, "pixhawk4-fmu-v5") != 0 ||
	    strcmp(node->matched_compatible, "holybro,pixhawk4") != 0) {
		return -1;
	}
	/*
	 * The adapter is selected by the DT registry, but deliberately reuses the
	 * established stateful FMUv5 construction until its individual models are
	 * fully source-migrated.  The no-DT caller below remains unchanged.
	 */
	board_attach();
	return 0;
}

static const AthrillDeviceTargetOps target_ops = {
	ATHRILL_DEVICE_ABI_MAJOR, ATHRILL_DEVICE_ABI_MINOR, sizeof(AthrillDeviceTargetOps),
	target_mmio_register, target_mmio_unregister, target_irq_resolve, target_irq_set, target_irq_active,
	target_time_now, target_time_frequency, target_clock_frequency, target_memory_read, target_memory_write,
	target_spi_bind, target_spi_select, target_spi_exchange, target_i2c_bind, target_i2c_transfer,
	target_uart_bind, target_uart_peripheral, target_uart_controller, target_gpio_resolve, target_gpio_read,
	target_gpio_drive, target_gpio_subscribe, target_dma_resolve, target_dma_request,
	target_model_adapter_connect
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

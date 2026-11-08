/*
 * See target_host.h: the STM32 target's implementation of the athrill-device
 * required interface. Moved unchanged from device_tree.c (only the names and
 * the owner of the MMIO adapter table changed).
 *
 * A model whose register access must end the CPU batch reports it itself
 * (end_batch); the target keeps no list of such addresses.
 *
 * Copyright 2026 Hakoniwa Laboratory
 * SPDX-License-Identifier: MIT
 */
#include "target_host.h"

#include "bus.h"
#include "concrete_executor/target/dbg_target_serial.h"
#include "cpuemu_ops.h"
#include "device.h"
#include "mpu.h"
#include "target_cpu.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_MMIO_ADAPTERS 64U
#define MAX_HOST_VALUES 32U

typedef struct {
	const AthrillDeviceMmioOps *ops;
	void *instance;
	uint32 region_index;
	std_bool active;
} MmioAdapter;

static MmioAdapter mmio[MAX_MMIO_ADAPTERS];

typedef struct {
	char key[64];
	char value[32];
} NumericHostValue;
static NumericHostValue numeric_host_values[MAX_HOST_VALUES];
static uint32 numeric_host_value_count;

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
	stm32_device_accessed = TRUE;
	return (!adapter->active ||
	        adapter->ops->write(adapter->instance, address, 1U, data) != 0) ? STD_E_INVALID : STD_E_OK;
}
static Std_ReturnType mmio_put16(MpuAddressRegionType *region, CoreIdType core,
                                 AthrillAddrType address, uint16 data)
{
	MmioAdapter *adapter = mmio_of(region);
	(void)core;
	stm32_device_accessed = TRUE;
	return (!adapter->active ||
	        adapter->ops->write(adapter->instance, address, 2U, data) != 0) ? STD_E_INVALID : STD_E_OK;
}
static Std_ReturnType mmio_put32(MpuAddressRegionType *region, CoreIdType core,
                                 AthrillAddrType address, uint32 data)
{
	MmioAdapter *adapter = mmio_of(region);
	(void)core;
	stm32_device_accessed = TRUE;
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

sint32 stm32_host_mmio_register(void *context, uint64 address, uint64 size,
                                   const AthrillDeviceMmioOps *ops, void *instance, AthrillDeviceMmioToken *token)
{
	(void)context;
	uint32 i;
	MpuAddressRegionType *maps;
	if (address > 0xffffffffULL || size == 0U || size > 0xffffffffULL ||
	    address + size > 0x100000000ULL) {
		return -1;
	}
	for (i = 0U; i < MAX_MMIO_ADAPTERS; i++)
		if (!mmio[i].active) {
			break;
		}
	if (i == MAX_MMIO_ADAPTERS) {
		return -1;
	}
	mmio[i].ops = ops;
	mmio[i].instance = instance;
	mmio[i].active = TRUE;
	maps = realloc(mpu_address_map.dynamic_map, sizeof(*maps) * (mpu_address_map.dynamic_map_num + 1U));
	if (maps == NULL) {
		mmio[i].active = FALSE;
		return -1;
	}
	mpu_address_map.dynamic_map = maps;
	mmio[i].region_index = mpu_address_map.dynamic_map_num;
	maps[mmio[i].region_index].type = DEVICE;
	maps[mmio[i].region_index].is_malloc = FALSE;
	maps[mmio[i].region_index].permission = MPU_ADDRESS_REGION_PERM_ALL;
	maps[mmio[i].region_index].start = (AthrillAddrType)address;
	maps[mmio[i].region_index].size = (AthrillAddrType)size;
	maps[mmio[i].region_index].mask = MPU_ADDRESS_REGION_MASK_ALL;
	maps[mmio[i].region_index].data = (uint8 *)&mmio[i];
	maps[mmio[i].region_index].ops = &mmio_region_ops;
	mpu_address_map.dynamic_map_num++;
	token->value = (uint64)i + 1U;
	return 0;
}

void stm32_host_mmio_unregister(void *context, AthrillDeviceMmioToken token)
{
	(void)context;
	uint64 i = token.value;
	if (i == 0U || i > MAX_MMIO_ADAPTERS) {
		return;
	}
	i--;
	if (mmio[i].active) {
		mmio[i].active = FALSE;
		if (mmio[i].region_index < mpu_address_map.dynamic_map_num) {
			mpu_address_map.dynamic_map[mmio[i].region_index].size = 0U;
		}
	}
}

sint32 stm32_host_irq_resolve(void *context, const AthrillDeviceSpecifier *spec,
                                 AthrillDeviceIrqLine *line)
{
	(void)context;
	if (spec == NULL || spec->cell_count == 0U) {
		return -1;
	}
	line->value = spec->cells[0];
	return line->value < virtual_cpu.cores[0].core.m.num_irq ? 0 : -1;
}
sint32 stm32_host_irq_set(void *context, AthrillDeviceIrqLine line, std_bool asserted)
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
std_bool stm32_host_irq_is_active(void *context, AthrillDeviceIrqLine line)
{
	(void)context;
	return line.value <= 0xffffffffU ? intc_irq_active((uint32)line.value) : FALSE;
}
uint64 stm32_host_time_now(void *context)
{
	(void)context;
	return cpuemu_get_clock();
}
uint64 stm32_host_time_frequency_hz(void *context)
{
	uint32 mhz = 216U;
	(void)context;
	(void)cpuemu_get_devcfg_value("DEVICE_CPU_FREQ", &mhz);
	return (uint64)mhz * 1000000U;
}
sint32 stm32_host_clock_frequency_hz(void *context, AthrillDeviceNodeId node,
                                     const char *clock_name, uint64 *frequency)
{
	(void)context;
	(void)node;
	(void)clock_name;
	*frequency = stm32_host_time_frequency_hz(NULL);
	return 0;
}

sint32 stm32_host_memory_read(void *context, uint64 address, void *data, uint32 size)
{
	uint32 i;
	(void)context;
	if (address > 0xffffffffULL || address + size > 0x100000000ULL) {
		return -1;
	}
	/* 1, 2 or 4 bytes: one access of that width (a peripheral data register
	 * is read once, as by the CPU); a larger size is read byte by byte. */
	if (size == 2U) {
		uint16 v16;
		if (bus_get_data16(0U, (AthrillAddrType)address, &v16) != STD_E_OK) {
			return -1;
		}
		memcpy(data, &v16, 2U);
		return 0;
	}
	if (size == 4U) {
		uint32 v32;
		if (bus_get_data32(0U, (AthrillAddrType)address, &v32) != STD_E_OK) {
			return -1;
		}
		memcpy(data, &v32, 4U);
		return 0;
	}
	for (i = 0U; i < size;
	     i++)
		if (bus_get_data8(0U, (AthrillAddrType)(address + i),
		                  &((uint8 *)data)[i]) != STD_E_OK) {
			return -1;
		}
	return 0;
}
sint32 stm32_host_memory_write(void *context, uint64 address, const void *data, uint32 size)
{
	uint32 i;
	(void)context;
	if (address > 0xffffffffULL || address + size > 0x100000000ULL) {
		return -1;
	}
	/* 1, 2 or 4 bytes: one access of that width; a larger size byte by byte. */
	if (size == 2U) {
		uint16 v16;
		memcpy(&v16, data, 2U);
		return (bus_put_data16(0U, (AthrillAddrType)address, v16) == STD_E_OK) ? 0 : -1;
	}
	if (size == 4U) {
		uint32 v32;
		memcpy(&v32, data, 4U);
		return (bus_put_data32(0U, (AthrillAddrType)address, v32) == STD_E_OK) ? 0 : -1;
	}
	for (i = 0U; i < size;
	     i++)
		if (bus_put_data8(0U, (AthrillAddrType)(address + i),
		                  ((const uint8 *)data)[i]) != STD_E_OK) {
			return -1;
		}
	return 0;
}

Std_ReturnType stm32_host_config_string(const char *key, char **value)
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

sint32 stm32_host_value(void *context, const char *key,
	const char **value)
{
	char *configured = NULL;
	uint32 number;
	uint32 i;
	(void)context;
	if (key == NULL || value == NULL) {
		return -1;
	}
	if (stm32_host_config_string(key, &configured) == STD_E_OK) {
		*value = configured;
		return 0;
	}
	for (i = 0U; i < numeric_host_value_count; i++) {
		if (strcmp(numeric_host_values[i].key, key) == 0) {
			*value = numeric_host_values[i].value;
			return 0;
		}
	}
	if ((cpuemu_get_devcfg_value(key, &number) != STD_E_OK &&
	     cpuemu_get_devcfg_value_hex(key, &number) != STD_E_OK) ||
	    numeric_host_value_count >= MAX_HOST_VALUES || strlen(key) >=
	    sizeof(numeric_host_values[0].key)) {
		return -1;
	}
	strcpy(numeric_host_values[numeric_host_value_count].key, key);
	snprintf(numeric_host_values[numeric_host_value_count].value,
		sizeof(numeric_host_values[0].value), "%u", number);
	*value = numeric_host_values[numeric_host_value_count].value;
	numeric_host_value_count++;
	return 0;
}

sint32 stm32_host_console_write(void *context, uint32 channel, uint8 data)
{
	(void)context;
	if (channel > 0xFFU) {
		return -1;
	}
	return dbg_serial_putchar((uint8)channel, data) ? 0 : -1;
}

void stm32_host_end_batch(void *context)
{
	(void)context;
	stm32_device_accessed = TRUE;
}

void stm32_host_reset_values(void)
{
	numeric_host_value_count = 0U;
}


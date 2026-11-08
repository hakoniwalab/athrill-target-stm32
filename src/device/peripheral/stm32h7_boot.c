/*
 * The STM32H743 boot-time regions: empty since step 4-7 (see below).
 */
#include "stm32h7_boot.h"
#include "device.h"

uint8 stm32h7_otg_fs_data[STM32H7_OTG_FS_SIZE];
uint8 stm32h7_ahb2_data[STM32H7_AHB2_SIZE];
uint8 stm32h7_axi_data[STM32H7_AXI_SIZE];
uint8 stm32h7_d1_data[STM32H7_D1_SIZE];
uint8 stm32h7_d3_data[STM32H7_D3_SIZE];
uint8 stm32h7_dbgmcu_data[STM32H7_DBGMCU_SIZE];
uint8 stm32h7_sysmem_high_data[STM32H7_SYSMEM_HIGH_SIZE];

/*
 * Every H7 boot-time block is an athrill-device model now (register blocks
 * with Devicetree read rules, hakoniwalab/athrill-device#13 step 4-7): an
 * access that reaches these regions is to no described peripheral, and
 * fails. The regions and this file go in step 4-8.
 */
Std_ReturnType stm32h7_boot_read(uint32 addr, uint32 size, uint32 *value)
{
	(void)addr;
	(void)size;
	*value = 0U;
	return STD_E_SEGV;
}

Std_ReturnType stm32h7_boot_write(uint32 addr, uint32 size, uint32 value)
{
	(void)addr;
	(void)size;
	(void)value;
	return STD_E_SEGV;
}

void stm32h7_boot_reset(void)
{
}

static Std_ReturnType get8(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 *d)
{
	uint32 value;
	Std_ReturnType err;
	(void)r;
	(void)c;
	err = stm32h7_boot_read((uint32)a, 1U, &value);
	*d = (uint8)value;
	return err;
}

static Std_ReturnType get16(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint16 *d)
{
	uint32 value;
	Std_ReturnType err;
	(void)r;
	(void)c;
	err = stm32h7_boot_read((uint32)a, 2U, &value);
	*d = (uint16)value;
	return err;
}

static Std_ReturnType get32(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint32 *d)
{
	(void)r;
	(void)c;
	return stm32h7_boot_read((uint32)a, 4U, d);
}

static Std_ReturnType put8(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 d)
{
	(void)r;
	(void)c;
	stm32_device_accessed = TRUE;
	return stm32h7_boot_write((uint32)a, 1U, d);
}

static Std_ReturnType put16(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint16 d)
{
	(void)r;
	(void)c;
	stm32_device_accessed = TRUE;
	return stm32h7_boot_write((uint32)a, 2U, d);
}

static Std_ReturnType put32(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint32 d)
{
	(void)r;
	(void)c;
	stm32_device_accessed = TRUE;
	return stm32h7_boot_write((uint32)a, 4U, d);
}

MpuAddressRegionOperationType stm32h7_boot_ops = {
	get8, get16, get32, put8, put16, put32, NULL,
};

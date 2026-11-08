/*
 * Explicit STM32H743 boot-time peripherals from RM0433.  Clock and regulator
 * ready bits complete immediately in virtual time.  Other listed registers
 * retain the bytes written to them; addresses outside the listed peripherals
 * are rejected instead of becoming a generic register file.
 */
#include "stm32h7_boot.h"
#include "board/board_profile.h"
#include "device.h"
#include <string.h>

#define H7_D2_BASE 0x40000000U
#define H7_D2_SIZE 0x00080000U

#define SYSCFG_BASE    0x58000400U
#define FDCAN1_BASE    0x4000A000U
#define FDCAN2_BASE    0x4000A400U
#define FDCAN_RAM_BASE 0x4000AC00U
#define FDCAN_RAM_SIZE 0x00002800U
#define BDMA_BASE      0x58025400U
#define AXI_TARG7_BASE 0x51008000U
#define AXI_TARG7_SIZE 0x00000400U
#define SYSMEM_DATA_BASE 0x1FF1E800U
#define SYSMEM_DATA_SIZE 0x00000100U

uint8 stm32h7_otg_fs_data[STM32H7_OTG_FS_SIZE];
uint8 stm32h7_ahb2_data[STM32H7_AHB2_SIZE];
uint8 stm32h7_axi_data[STM32H7_AXI_SIZE];
uint8 stm32h7_d1_data[STM32H7_D1_SIZE];
uint8 stm32h7_d3_data[STM32H7_D3_SIZE];
uint8 stm32h7_dbgmcu_data[STM32H7_DBGMCU_SIZE];
uint8 stm32h7_sysmem_high_data[STM32H7_SYSMEM_HIGH_SIZE];

static uint8 h7_d2_data[H7_D2_SIZE];

typedef struct {
	uint32 base;
	uint32 size;
	uint8 *data;
} H7RegionType;

static const H7RegionType regions[] = {
	{ FDCAN1_BASE, 0x400U, &h7_d2_data[FDCAN1_BASE - H7_D2_BASE] },
	{ FDCAN2_BASE, 0x400U, &h7_d2_data[FDCAN2_BASE - H7_D2_BASE] },
	{ FDCAN_RAM_BASE, FDCAN_RAM_SIZE,
		&h7_d2_data[FDCAN_RAM_BASE - H7_D2_BASE] },
	{ AXI_TARG7_BASE, AXI_TARG7_SIZE,
		&stm32h7_axi_data[AXI_TARG7_BASE - STM32H7_AXI_BASE] },
	{ SYSCFG_BASE, 0x400U, &stm32h7_d3_data[SYSCFG_BASE - STM32H7_D3_BASE] },
	{ BDMA_BASE, 0x400U, &stm32h7_d3_data[BDMA_BASE - STM32H7_D3_BASE] },
	{ STM32H7_DBGMCU_BASE, STM32H7_DBGMCU_SIZE, stm32h7_dbgmcu_data },
	{ SYSMEM_DATA_BASE, SYSMEM_DATA_SIZE,
		&stm32h7_sysmem_high_data[SYSMEM_DATA_BASE - STM32H7_SYSMEM_HIGH_BASE] },
};

static const H7RegionType *region_of(uint32 addr, uint32 size)
{
	uint32 i;

	for (i = 0U; i < (sizeof(regions) / sizeof(regions[0])); i++) {
		uint32 offset = addr - regions[i].base;

		if ((offset < regions[i].size) && ((regions[i].size - offset) >= size)) {
			return &regions[i];
		}
	}
	return NULL;
}

static uint32 stored_word(const H7RegionType *region, uint32 addr)
{
	uint32 value;
	uint32 aligned = (addr - region->base) & ~3U;

	memcpy(&value, &region->data[aligned], sizeof(value));
	return value;
}

static void store_bytes(const H7RegionType *region, uint32 addr, uint32 size, uint32 value)
{
	uint32 offset = addr - region->base;
	uint32 i;

	for (i = 0U; i < size; i++) {
		region->data[offset + i] = (uint8)(value >> (8U * i));
	}
}

Std_ReturnType stm32h7_boot_read(uint32 addr, uint32 size, uint32 *value)
{
	const H7RegionType *region;
	uint32 v;

	if (!stm32_board_is_h7()) {
		return STD_E_SEGV;
	}
	region = region_of(addr, size);
	if (region == NULL) {
		return STD_E_SEGV;
	}
	v = stored_word(region, addr & ~3U);
	*value = (v >> (8U * (addr & 3U)))
		& ((size >= 4U) ? 0xFFFFFFFFU : ((1U << (8U * size)) - 1U));
	return STD_E_OK;
}

Std_ReturnType stm32h7_boot_write(uint32 addr, uint32 size, uint32 value)
{
	const H7RegionType *region;

	if (!stm32_board_is_h7()) {
		return STD_E_SEGV;
	}
	region = region_of(addr, size);
	if (region == NULL) {
		return STD_E_SEGV;
	}
	store_bytes(region, addr, size, value);
	return STD_E_OK;
}

void stm32h7_boot_reset(void)
{
	uint32 i;
	static const uint8 uid[12] = {
		0x41U, 0x54U, 0x48U, 0x52U, 0x49U, 0x4CU,
		0x4CU, 0x20U, 0x48U, 0x37U, 0x34U, 0x33U,
	};

	for (i = 0U; i < (sizeof(regions) / sizeof(regions[0])); i++) {
		memset(regions[i].data, 0, regions[i].size);
	}

	/* STM32H743 device ID/revision and 2048 KiB flash size. */
	stm32h7_dbgmcu_data[0] = 0x50U;
	stm32h7_dbgmcu_data[1] = 0x04U;
	stm32h7_dbgmcu_data[2] = 0x03U;
	stm32h7_dbgmcu_data[3] = 0x20U;
	memcpy(&stm32h7_sysmem_high_data[0xE800U], uid, sizeof(uid));
	stm32h7_sysmem_high_data[0xE880U] = 0x00U;
	stm32h7_sysmem_high_data[0xE881U] = 0x08U;
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

/*
 * Explicit STM32H743 boot-time peripherals from RM0433.  Clock and regulator
 * ready bits complete immediately in virtual time.  Other listed registers
 * retain the bytes written to them; addresses outside the listed peripherals
 * are rejected instead of becoming a generic register file.
 */
#include "stm32h7_boot.h"
#include "stm32h7_sdmmc.h"
#include "board/board_profile.h"
#include "device.h"
#include <string.h>

#define H7_D2_BASE 0x40000000U
#define H7_D2_SIZE 0x00080000U

#define RCC_BASE       0x58024400U
#define RCC_CR         (RCC_BASE + 0x00U)
#define RCC_CFGR       (RCC_BASE + 0x10U)
#define RCC_BDCR       (RCC_BASE + 0x70U)
#define RCC_CSR        (RCC_BASE + 0x74U)
#define PWR_BASE       0x58024800U
#define PWR_CSR1       (PWR_BASE + 0x04U)
#define PWR_CR3        (PWR_BASE + 0x0CU)
#define PWR_D3CR       (PWR_BASE + 0x18U)
#define FLASH_BASE     0x52002000U
#define FLASH_ACR      (FLASH_BASE + 0x00U)
#define FLASH_SR1      (FLASH_BASE + 0x10U)
#define FLASH_SR2      (FLASH_BASE + 0x90U)
#define SYSCFG_BASE    0x58000400U
#define RTC_BASE       0x58004000U
#define RTC_ICSR       (RTC_BASE + 0x0CU)
#define IWDG_BASE      0x58004800U
#define OTG_FS_GRSTCTL (STM32H7_OTG_FS_BASE + 0x10U)
#define ADC12_BASE     0x40022000U
#define FDCAN1_BASE    0x4000A000U
#define FDCAN2_BASE    0x4000A400U
#define FDCAN_RAM_BASE 0x4000AC00U
#define FDCAN_RAM_SIZE 0x00002800U
#define ADC3_BASE      0x58026000U
#define ADC_ISR        0x00U
#define ADC_CR         0x08U
#define ADC_DR         0x40U
#define RNG_BASE       0x48021800U
#define RNG_SR         (RNG_BASE + 0x04U)
#define RNG_DR         (RNG_BASE + 0x08U)
#define SDMMC2_BASE    0x48022400U
#define SDMMC_STA      (SDMMC2_BASE + 0x34U)
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
static uint32 rng_value;

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
	{ ADC12_BASE, 0x400U, &h7_d2_data[ADC12_BASE - H7_D2_BASE] },
	{ STM32H7_OTG_FS_BASE, STM32H7_OTG_FS_SIZE, stm32h7_otg_fs_data },
	{ RNG_BASE, 0x400U, &stm32h7_ahb2_data[RNG_BASE - STM32H7_AHB2_BASE] },
	{ AXI_TARG7_BASE, AXI_TARG7_SIZE,
		&stm32h7_axi_data[AXI_TARG7_BASE - STM32H7_AXI_BASE] },
	{ FLASH_BASE, 0x400U, &stm32h7_d1_data[FLASH_BASE - STM32H7_D1_BASE] },
	{ SYSCFG_BASE, 0x400U, &stm32h7_d3_data[SYSCFG_BASE - STM32H7_D3_BASE] },
	{ RTC_BASE, 0x400U, &stm32h7_d3_data[RTC_BASE - STM32H7_D3_BASE] },
	{ IWDG_BASE, 0x400U, &stm32h7_d3_data[IWDG_BASE - STM32H7_D3_BASE] },
	{ RCC_BASE, 0x400U, &stm32h7_d3_data[RCC_BASE - STM32H7_D3_BASE] },
	{ PWR_BASE, 0x400U, &stm32h7_d3_data[PWR_BASE - STM32H7_D3_BASE] },
	{ BDMA_BASE, 0x400U, &stm32h7_d3_data[BDMA_BASE - STM32H7_D3_BASE] },
	{ ADC3_BASE, 0x400U, &stm32h7_d3_data[ADC3_BASE - STM32H7_D3_BASE] },
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

static uint32 adc_read(const H7RegionType *region, uint32 addr, uint32 value)
{
	uint32 base = (addr >= ADC3_BASE) ? ADC3_BASE : ADC12_BASE;
	uint32 offset = (addr & ~3U) - base;
	uint32 cr = stored_word(region, base + ADC_CR);

	if (offset == ADC_ISR) {
		/* ADRDY after ADEN, EOC/EOS after a conversion start. */
		value |= ((cr & 1U) != 0U) ? 1U : 0U;
		value |= ((cr & (1U << 2)) != 0U) ? ((1U << 2) | (1U << 3)) : 0U;
	}
	else if (offset == ADC_CR) {
		/* Hardware clears ADCAL and ADDIS when their actions complete. */
		value &= ~((1U << 31) | (1U << 1));
	}
	else if (offset == ADC_DR) {
		/* A deterministic low board-ID voltage selects the baseline revision. */
		value = 0U;
	}
	return value;
}

Std_ReturnType stm32h7_boot_read(uint32 addr, uint32 size, uint32 *value)
{
	const H7RegionType *region;
	uint32 a = addr & ~3U;
	uint32 v;

	if (!stm32_board_is_h7()) {
		return STD_E_SEGV;
	}
	if (stm32h7_sdmmc_owns(addr)) {
		v = stm32h7_sdmmc_read(addr);
		*value = (v >> (8U * (addr & 3U)))
			& ((size >= 4U) ? 0xFFFFFFFFU : ((1U << (8U * size)) - 1U));
		return STD_E_OK;
	}
	region = region_of(addr, size);
	if (region == NULL) {
		return STD_E_SEGV;
	}
	v = stored_word(region, a);
	switch (a) {
	case RCC_CR:
		/* HSIRDY, CSIRDY, HSI48RDY, HSERDY and PLL1/2/3RDY follow their ON bits. */
		v |= ((v & (1U << 0)) != 0U) ? (1U << 2) : 0U;
		v |= (v & ((1U << 7) | (1U << 12) | (1U << 16)
			| (1U << 24) | (1U << 26) | (1U << 28))) << 1;
		break;
	case RCC_CFGR:
		v = (v & ~(7U << 3)) | ((v & 7U) << 3); /* SWS = SW */
		break;
	case RCC_BDCR:
	case RCC_CSR:
		v |= (v & 1U) << 1;
		break;
	case PWR_CSR1:
	case PWR_D3CR:
		v |= 1U << 13; /* ACTVOSRDY / VOSRDY */
		break;
	case PWR_CR3:
		v |= (v & (1U << 24)) << 2; /* USB33RDY follows USB33DEN */
		break;
	case FLASH_SR1:
	case FLASH_SR2:
		v &= 0x0000FFFFU; /* QW/CRC busy are clear */
		break;
	case RTC_ICSR:
		v |= (1U << 5) | ((v & (1U << 7)) >> 1); /* RSF, INITF */
		break;
	case OTG_FS_GRSTCTL:
		v = (v & ~0x31U) | (1U << 31); /* resets complete, AHB idle */
		break;
	case RNG_SR:
		v |= 1U; /* DRDY */
		break;
	case RNG_DR:
		rng_value = (rng_value * 1664525U) + 1013904223U;
		v = rng_value;
		break;
	case SDMMC_STA:
		/* A no-response command is sent; response commands time out with no card. */
		v |= (1U << 2) | (1U << 7);
		break;
	default:
		if (((a >= ADC12_BASE) && (a < (ADC12_BASE + 0x400U)))
			|| ((a >= ADC3_BASE) && (a < (ADC3_BASE + 0x400U)))) {
			v = adc_read(region, a, v);
		}
		break;
	}
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
	if (stm32h7_sdmmc_owns(addr)) {
		stm32h7_sdmmc_write(addr, size, value);
		return STD_E_OK;
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
	rng_value = 0x7436C001U;

	/* STM32H743 device ID/revision and 2048 KiB flash size. */
	stm32h7_dbgmcu_data[0] = 0x50U;
	stm32h7_dbgmcu_data[1] = 0x04U;
	stm32h7_dbgmcu_data[2] = 0x03U;
	stm32h7_dbgmcu_data[3] = 0x20U;
	memcpy(&stm32h7_sysmem_high_data[0xE800U], uid, sizeof(uid));
	stm32h7_sysmem_high_data[0xE880U] = 0x00U;
	stm32h7_sysmem_high_data[0xE881U] = 0x08U;

	/* Reset clock source: HSI on and ready. */
	store_bytes(region_of(RCC_CR, 4U), RCC_CR, 4U, (1U << 0) | (1U << 2));
	store_bytes(region_of(FLASH_ACR, 4U), FLASH_ACR, 4U, 0U);
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

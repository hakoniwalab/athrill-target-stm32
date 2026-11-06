/*
 * Early STM32H743 peripherals from RM0433.  Clock and regulator ready bits
 * complete immediately in virtual time.  Other early registers retain the
 * bytes written to them.  The first access to an unmodelled 1 KiB block is
 * reported as an H7-STUB entry for the next implementation stage.
 */
#include "stm32h7_boot.h"
#include "board/board_profile.h"
#include "device.h"
#include <stdio.h>
#include <string.h>

#define H7_D2_BASE 0x40000000U
#define H7_D2_SIZE 0x00080000U
#define H7_F7_AHB2_BASE 0x50000000U
#define H7_F7_AHB2_SIZE 0x00070000U
#define H7_SYSMEM_LOW_BASE 0x1FF00000U
#define H7_SYSMEM_LOW_SIZE 0x00010000U

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
#define OTG_FS_GRSTCTL (STM32H7_OTG_FS_BASE + 0x10U)
#define ADC12_BASE     0x40022000U
#define ADC3_BASE      0x58026000U
#define ADC_ISR        0x00U
#define ADC_CR         0x08U
#define ADC_DR         0x40U
#define RNG_BASE       0x48021800U
#define RNG_SR         (RNG_BASE + 0x04U)
#define RNG_DR         (RNG_BASE + 0x08U)
#define SDMMC2_BASE    0x48022400U
#define SDMMC_STA      (SDMMC2_BASE + 0x34U)
#define SPI1_BASE      0x40013000U
#define SPI2_BASE      0x40003800U
#define SPI_SR_OFFSET  0x14U
#define SPI_RXDR_OFFSET 0x30U
#define I2C1_BASE      0x40005400U
#define I2C2_BASE      0x40005800U
#define I2C4_BASE      0x58001C00U
#define I2C_CR2_OFFSET 0x04U
#define I2C_ISR_OFFSET 0x18U
#define I2C_ICR_OFFSET 0x1CU

#define GPIO_BASE      0x58020000U
#define GPIO_END       0x58022C00U

uint8 stm32h7_otg_fs_data[STM32H7_OTG_FS_SIZE];
uint8 stm32h7_ahb2_data[STM32H7_AHB2_SIZE];
uint8 stm32h7_axi_data[STM32H7_AXI_SIZE];
uint8 stm32h7_d1_data[STM32H7_D1_SIZE];
uint8 stm32h7_d3_data[STM32H7_D3_SIZE];
uint8 stm32h7_dbgmcu_data[STM32H7_DBGMCU_SIZE];
uint8 stm32h7_sysmem_high_data[STM32H7_SYSMEM_HIGH_SIZE];

static uint8 h7_d2_data[H7_D2_SIZE];
static uint8 h7_f7_ahb2_data[H7_F7_AHB2_SIZE];
static uint8 h7_sysmem_low_data[H7_SYSMEM_LOW_SIZE];

#define REPORTED_BLOCKS 256U
static uint32 reported_blocks[REPORTED_BLOCKS];
static uint32 reported_count;
static uint32 rng_value;

typedef struct {
	uint32 base;
	uint32 size;
	uint8 *data;
} H7RegionType;

static const H7RegionType regions[] = {
	{ H7_D2_BASE, H7_D2_SIZE, h7_d2_data },
	{ STM32H7_OTG_FS_BASE, STM32H7_OTG_FS_SIZE, stm32h7_otg_fs_data },
	{ STM32H7_AHB2_BASE, STM32H7_AHB2_SIZE, stm32h7_ahb2_data },
	{ H7_F7_AHB2_BASE, H7_F7_AHB2_SIZE, h7_f7_ahb2_data },
	{ STM32H7_AXI_BASE, STM32H7_AXI_SIZE, stm32h7_axi_data },
	{ STM32H7_D1_BASE, STM32H7_D1_SIZE, stm32h7_d1_data },
	{ STM32H7_D3_BASE, STM32H7_D3_SIZE, stm32h7_d3_data },
	{ STM32H7_DBGMCU_BASE, STM32H7_DBGMCU_SIZE, stm32h7_dbgmcu_data },
	{ H7_SYSMEM_LOW_BASE, H7_SYSMEM_LOW_SIZE, h7_sysmem_low_data },
	{ STM32H7_SYSMEM_HIGH_BASE, STM32H7_SYSMEM_HIGH_SIZE, stm32h7_sysmem_high_data },
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

static std_bool f7_only_address(uint32 addr)
{
	/* These H7-reserved blocks are the F7 PWR, SYSCFG/EXTI, RCC and FLASH. */
	return ((addr >= 0x40007000U) && (addr < 0x40007400U))
		|| ((addr >= 0x40013800U) && (addr < 0x40014000U))
		|| ((addr >= 0x40023800U) && (addr < 0x40024000U));
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

static std_bool modelled_block(uint32 addr)
{
	return ((addr >= RCC_BASE) && (addr < (RCC_BASE + 0x400U)))
		|| ((addr >= PWR_BASE) && (addr < (PWR_BASE + 0x400U)))
		|| ((addr >= FLASH_BASE) && (addr < (FLASH_BASE + 0x400U)))
		|| ((addr >= SYSCFG_BASE) && (addr < (SYSCFG_BASE + 0x400U)))
		|| ((addr >= GPIO_BASE) && (addr < GPIO_END))
		|| ((addr >= RTC_BASE) && (addr < (RTC_BASE + 0x400U)))
		|| ((addr >= ADC12_BASE) && (addr < (ADC12_BASE + 0x400U)))
		|| ((addr >= ADC3_BASE) && (addr < (ADC3_BASE + 0x400U)))
		|| ((addr >= RNG_BASE) && (addr < (RNG_BASE + 0x400U)))
		|| ((addr >= SDMMC2_BASE) && (addr < (SDMMC2_BASE + 0x400U)))
		|| ((addr >= STM32H7_DBGMCU_BASE) && (addr < (STM32H7_DBGMCU_BASE + STM32H7_DBGMCU_SIZE)))
		|| ((addr >= H7_SYSMEM_LOW_BASE) && (addr < (H7_SYSMEM_LOW_BASE + H7_SYSMEM_LOW_SIZE)))
		|| ((addr >= STM32H7_SYSMEM_HIGH_BASE) && (addr < (STM32H7_SYSMEM_HIGH_BASE + STM32H7_SYSMEM_HIGH_SIZE)));
}

static void report_stub(uint32 addr, const char *what)
{
	uint32 block = addr & ~0x3FFU;
	uint32 i;

	if (modelled_block(addr)) {
		return;
	}
	for (i = 0U; i < reported_count; i++) {
		if (reported_blocks[i] == block) {
			return;
		}
	}
	if (reported_count < REPORTED_BLOCKS) {
		reported_blocks[reported_count++] = block;
	}
	fprintf(stderr, "H7-STUB: first %s at 0x%08x (block 0x%08x)\n", what, addr, block);
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

static std_bool spi_register(uint32 addr, uint32 offset)
{
	return (addr == (SPI1_BASE + offset)) || (addr == (SPI2_BASE + offset));
}

static std_bool i2c_register(uint32 addr, uint32 offset)
{
	return (addr == (I2C1_BASE + offset)) || (addr == (I2C2_BASE + offset))
		|| (addr == (I2C4_BASE + offset));
}

Std_ReturnType stm32h7_boot_read(uint32 addr, uint32 size, uint32 *value)
{
	const H7RegionType *region;
	uint32 a = addr & ~3U;
	uint32 v;

	if (!stm32_board_is_h7() || f7_only_address(addr)) {
		return STD_E_SEGV;
	}
	region = region_of(addr, size);
	if (region == NULL) {
		return STD_E_SEGV;
	}
	report_stub(addr, "read");
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
		v |= 1U << 2; /* no card attached: command-response timeout */
		break;
	default:
		if (spi_register(a, SPI_SR_OFFSET)) {
			/* TXP, RXP and TXC let a polled transfer complete. */
			v |= (1U << 0) | (1U << 1) | (1U << 11);
		}
		else if (spi_register(a, SPI_RXDR_OFFSET)) {
			v = 0xFFFFFFFFU; /* no chip selected */
		}
		else if (((a >= ADC12_BASE) && (a < (ADC12_BASE + 0x400U)))
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
	uint32 a = addr & ~3U;

	if (!stm32_board_is_h7() || f7_only_address(addr)) {
		return STD_E_SEGV;
	}
	region = region_of(addr, size);
	if (region == NULL) {
		return STD_E_SEGV;
	}
	report_stub(addr, "write");
	store_bytes(region, addr, size, value);
	if (i2c_register(a, I2C_CR2_OFFSET) && ((value & (1U << 13)) != 0U)) {
		/* No chip is attached in stage 1: START completes with NACK and STOP. */
		store_bytes(region_of(a + (I2C_ISR_OFFSET - I2C_CR2_OFFSET), 4U),
			a + (I2C_ISR_OFFSET - I2C_CR2_OFFSET), 4U, (1U << 0) | (1U << 4) | (1U << 5));
	}
	else if (i2c_register(a, I2C_ICR_OFFSET)) {
		uint32 isr_addr = a - (I2C_ICR_OFFSET - I2C_ISR_OFFSET);
		const H7RegionType *isr_region = region_of(isr_addr, 4U);
		uint32 isr = stored_word(isr_region, isr_addr);

		store_bytes(isr_region, isr_addr, 4U, isr & ~(value & 0x3F38U));
	}
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
	reported_count = 0U;
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
	store_bytes(region_of(I2C1_BASE + I2C_ISR_OFFSET, 4U), I2C1_BASE + I2C_ISR_OFFSET, 4U, 1U);
	store_bytes(region_of(I2C2_BASE + I2C_ISR_OFFSET, 4U), I2C2_BASE + I2C_ISR_OFFSET, 4U, 1U);
	store_bytes(region_of(I2C4_BASE + I2C_ISR_OFFSET, 4U), I2C4_BASE + I2C_ISR_OFFSET, 4U, 1U);
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

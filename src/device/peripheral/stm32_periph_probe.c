/*
 * PROVISIONAL, for investigation (see stm32_periph_probe.h): every register
 * keeps what is written to it, with the ready bits the start-up code waits
 * for (RCC oscillators and PLLs, the system clock switch, PWR over-drive,
 * the regulator, the RTC's synchronisation and initialisation, the USB OTG
 * core's resets, an SD card slot without a card, SPI buses with nothing on
 * them, an ADC whose conversions are always done); the timers PX4 needs are
 * modelled apart (stm32_tim.c), and so are the USART/UARTs (stm32_uart.c). The first access to each 1 KiB block of
 * registers is reported on stderr.
 */
#include "stm32_periph_probe.h"
#include "device.h"
#include "concrete_executor/target/dbg_target_serial.h"
#include "stm32_tim.h"
#include "stm32_uart.h"
#include "stm32_gpio.h"
#include "stm32_spi.h"
#include "stm32_exti.h"
#include "stm32_dma.h"
#include <stdio.h>
#include "std_device_ops.h"

uint8 stm32_probe_apb_ahb1_data[STM32_PROBE_APB_AHB1_SIZE];
uint8 stm32_probe_ahb2_data[STM32_PROBE_AHB2_SIZE];
uint8 stm32_probe_sysmem_data[STM32_PROBE_SYSMEM_SIZE];
uint8 stm32_probe_dbgmcu_data[STM32_PROBE_DBGMCU_SIZE];

void stm32_probe_init(void)
{
	static const uint8 uid[12] = { 0x41, 0x54, 0x48, 0x52, 0x49, 0x4C, 0x4C, 0x20, 0x53, 0x54, 0x4D, 0x37 };
	uint32 i;

	for (i = 0U; i < sizeof(uid); i++) {
		stm32_probe_sysmem_data[0xF420U + i] = uid[i];
	}
	stm32_probe_sysmem_data[0xF442U] = 0x00U;	/* 2048 KiB of flash */
	stm32_probe_sysmem_data[0xF443U] = 0x08U;
	/* DBGMCU_IDCODE: REV_ID 0x1001 (revision Z), DEV_ID 0x451 (STM32F76x/77x) */
	stm32_probe_dbgmcu_data[0] = 0x51U;
	stm32_probe_dbgmcu_data[1] = 0x64U;
	stm32_probe_dbgmcu_data[2] = 0x01U;
	stm32_probe_dbgmcu_data[3] = 0x10U;
}

#define RCC_CR		0x40023800U
#define RCC_CFGR	0x40023808U
#define RCC_BDCR	0x40023870U
#define RCC_CSR		0x40023874U
#define PWR_CR1		0x40007000U
#define PWR_CSR1	0x40007004U
#define FLASH_SR	0x40023C0CU
#define RTC_ISR		0x4000280CU
#define OTG_FS_GRSTCTL	0x50000010U
#define SDMMC1_STA	0x40012C34U
#define ADC1_SR		0x40012000U

static const uint32 spi_bases[] = {
	0x40013000U, 0x40003800U, 0x40003C00U, 0x40013400U, 0x40015000U, 0x40015400U,
};

static uint8 reported[(STM32_PROBE_APB_AHB1_SIZE + STM32_PROBE_AHB2_SIZE) / 1024U];

static void report(uint32 addr, const char *what)
{
	uint32 index;

	if ((addr < STM32_PROBE_APB_AHB1_BASE) || (addr >= 0xE0000000U)) {
		return;
	}
	index = (addr >= STM32_PROBE_AHB2_BASE)
		? ((STM32_PROBE_APB_AHB1_SIZE + (addr - STM32_PROBE_AHB2_BASE)) / 1024U) : ((addr - STM32_PROBE_APB_AHB1_BASE) / 1024U);

	if (reported[index] == 0U) {
		reported[index] = 1U;
		fprintf(stderr, "PROBE: first %s at 0x%08x\n", what, addr);
	}
}

static uint32 *word_at(MpuAddressRegionType *r, uint32 addr)
{
	return (uint32 *)&r->data[(addr & ~3U) - (uint32)r->start];
}

/* The value read, with the ready bits. */
static uint32 read_word(MpuAddressRegionType *r, uint32 addr)
{
	uint32 a = addr & ~3U;
	uint32 v = *word_at(r, a);
	uint32 i;

	if (stm32_tim_owns(a)) {
		return stm32_tim_read(a);
	}
	if (stm32_uart_owns(a)) {
		return stm32_uart_read(a);
	}
	if (stm32_gpio_owns(a)) {
		return stm32_gpio_read(a);
	}
	if (stm32_exti_owns(a)) {
		return stm32_exti_read(a);
	}
	if (stm32_dma_owns(a)) {
		return stm32_dma_read(a);
	}
	for (i = 0U; i < sizeof(spi_bases) / sizeof(spi_bases[0]); i++) {
		if (a == spi_bases[i] + 0x08U) {
			return v | 0x3U;	/* SR: TXE, RXNE */
		}
		if (a == spi_bases[i] + 0x0CU) {
			return 0xFFFFU;		/* DR: nothing on the bus */
		}
	}
	switch (a) {
	case RCC_CR:	/* HSIRDY, HSERDY, PLLRDY, PLLI2SRDY, PLLSAIRDY follow their ON bits */
		return v | ((v & 0x15010001U) << 1);
	case RCC_CFGR:	/* SWS = SW */
		return (v & ~0xCU) | ((v & 0x3U) << 2);
	case RCC_BDCR:	/* LSERDY = LSEON */
	case RCC_CSR:	/* LSIRDY = LSION */
		return v | ((v & 1U) << 1);
	case PWR_CSR1:	/* VOSRDY; ODRDY, ODSWRDY follow CR1's ODEN, ODSWEN; BRR follows BRE */
		return v | (1U << 14) | (*word_at(r, PWR_CR1) & (3U << 16)) | ((v & (1U << 9)) >> 6);
	case FLASH_SR:	/* not busy */
		return v & ~(1U << 16);
	case RTC_ISR:	/* RSF (registers synchronised); INITF follows INIT */
		return v | (1U << 5) | ((v & (1U << 7)) >> 1);
	case OTG_FS_GRSTCTL:	/* CSRST, RXFFLSH, TXFFLSH done; AHBIDL */
		return (v & ~0x31U) | (1U << 31);
	case SDMMC1_STA:	/* no card: every command sent, every response timed out */
		return (1U << 7) | (1U << 2);
	case ADC1_SR:	/* EOC: a conversion is always done (DR keeps 0) */
		return v | (1U << 1);
	default:
		return v;
	}
}

static void write_bytes(MpuAddressRegionType *r, uint32 addr, uint32 size, uint32 value)
{
	uint32 a = addr & ~3U;
	uint32 i;

	if (stm32_tim_owns(a)) {
		stm32_tim_write(addr, size, value);
		return;
	}
	if (stm32_uart_owns(a)) {
		stm32_uart_write(addr, size, value);
		return;
	}
	if (stm32_gpio_owns(a)) {
		stm32_gpio_write(addr, size, value);
		return;
	}
	if (stm32_exti_owns(a)) {
		stm32_exti_write(addr, size, value);
		return;
	}
	if (stm32_dma_owns(a)) {
		stm32_dma_write(addr, size, value);
		return;
	}
	if (stm32_spi_owns(a)) {
		stm32_spi_write(addr, size, value);
		return;
	}
	for (i = 0U; i < size; i++) {
		r->data[addr - (uint32)r->start + i] = (uint8)(value >> (8U * i));
	}
}

static Std_ReturnType get8(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 *d)
{
	(void)c;
	if (stm32_spi_owns((uint32)a)) {
		/* DR: a read takes from the RX FIFO as many frames as its size */
		stm32_device_accessed = TRUE;
		*d = (uint8)stm32_spi_read((uint32)a, 1U);
		return STD_E_OK;
	}
	if (stm32_uart_owns((uint32)a)) {
		stm32_device_accessed = TRUE;	/* RDR: the next character may arrive at once */
	}
	report((uint32)a, "read");
	*d = (uint8)(read_word(r, (uint32)a) >> (8U * (a & 3U)));
	return STD_E_OK;
}

static Std_ReturnType get16(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint16 *d)
{
	(void)c;
	if (stm32_spi_owns((uint32)a)) {
		/* DR: a read takes from the RX FIFO as many frames as its size */
		stm32_device_accessed = TRUE;
		*d = (uint16)stm32_spi_read((uint32)a, 2U);
		return STD_E_OK;
	}
	if (stm32_uart_owns((uint32)a)) {
		stm32_device_accessed = TRUE;	/* RDR: the next character may arrive at once */
	}
	report((uint32)a, "read");
	*d = (uint16)(read_word(r, (uint32)a) >> (8U * (a & 2U)));
	return STD_E_OK;
}

static Std_ReturnType get32(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint32 *d)
{
	(void)c;
	if (stm32_spi_owns((uint32)a)) {
		/* DR: a read takes from the RX FIFO as many frames as its size */
		stm32_device_accessed = TRUE;
		*d = (uint32)stm32_spi_read((uint32)a, 4U);
		return STD_E_OK;
	}
	if (stm32_uart_owns((uint32)a)) {
		stm32_device_accessed = TRUE;	/* RDR: the next character may arrive at once */
	}
	report((uint32)a, "read");
	*d = read_word(r, (uint32)a);
	return STD_E_OK;
}

static Std_ReturnType put8(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 d)
{
	(void)c;
	stm32_device_accessed = TRUE;
	report((uint32)a, "write");
	write_bytes(r, (uint32)a, 1U, d);
	return STD_E_OK;
}

static Std_ReturnType put16(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint16 d)
{
	(void)c;
	stm32_device_accessed = TRUE;
	report((uint32)a, "write");
	write_bytes(r, (uint32)a, 2U, d);
	return STD_E_OK;
}

static Std_ReturnType put32(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint32 d)
{
	(void)c;
	stm32_device_accessed = TRUE;
	report((uint32)a, "write");
	write_bytes(r, (uint32)a, 4U, d);
	return STD_E_OK;
}

static Std_ReturnType get_pointer(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 **d)
{
	(void)c;
	*d = &r->data[a - r->start];
	return STD_E_OK;
}

MpuAddressRegionOperationType stm32_probe_ops = { get8, get16, get32, put8, put16, put32, get_pointer };


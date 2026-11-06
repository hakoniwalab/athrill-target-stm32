#include "peripheral/stm32h7_spi.h"
#include "peripheral/stm32_spi.h"
#include "peripheral/stm32_gpio.h"
#include "peripheral/stm32_dmamux.h"
#include "memory/fm25v02.h"
#include <stdio.h>

#define GPIOD 0x58020C00U
#define SPI2  0x40003800U

static uint64 clock_now;

uint64 cpuemu_get_clock(void)
{
	return clock_now++;
}

static int failures;
static uint32 raised[128];

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int intc_raise_intr(uint32 intno)
{
	raised[intno]++;
	return 0;
}

int main(void)
{
	Fm25v02Type fram = { .fd = -1 };
	uint8 id[9];
	uint32 i;

	stm32_gpio_set_h7(TRUE);
	stm32_gpio_reset();
	stm32_spi_reset();
	stm32h7_spi_reset();
	stm32_dmamux_reset();
	fm25v02_open(&fram, NULL);
	stm32_spi_attach(2U, STM32_GPIO_PORT('D'), 4U, &fm25v02_ops, &fram);

	stm32_gpio_write(GPIOD, 4U, 1U << 8);       /* PD4 output */
	stm32_gpio_write(GPIOD + 0x18U, 2U, 1U << 4); /* chip select high */
	stm32_gpio_write(GPIOD + 0x1AU, 2U, 1U << 4); /* chip select low */
	stm32h7_spi_write(SPI2, 4U, 1U);             /* SPE */
	stm32h7_spi_write(SPI2 + 0x20U, 1U, 0x9FU);
	(void)stm32h7_spi_read(SPI2 + 0x30U, 1U);
	for (i = 0U; i < sizeof(id); i++) {
		stm32h7_spi_write(SPI2 + 0x20U, 1U, 0U);
		id[i] = (uint8)stm32h7_spi_read(SPI2 + 0x30U, 1U);
	}
	stm32_gpio_write(GPIOD + 0x18U, 2U, 1U << 4);
	CHECK(id[0] == 0x7FU && id[6] == 0xC2U && id[7] == 0x22U && id[8] == 0x08U);
	CHECK((stm32h7_spi_read(SPI2 + 0x14U, 4U) & ((1U << 1) | (1U << 12)))
		== ((1U << 1) | (1U << 12)));
	stm32h7_spi_write(SPI2 + 0x10U, 4U, 1U << 3); /* EOTIE after DMA completion */
	CHECK(raised[36] == 1U); /* SPI2 global IRQ wakes NuttX's TX-complete wait */

	stm32_dmamux_write(0x4002080CU, 4U, 39U); /* SPI2_RX request */
	stm32_dmamux_write(0x5802581CU, 4U, 80U); /* D3 request selector */
	CHECK(stm32_dmamux_read(0x4002080CU) == 39U);
	CHECK(stm32_dmamux_read(0x5802581CU) == 80U);

	if (failures == 0) {
		puts("STM32H7 SPI/DMAMUX tests passed");
	}
	return failures != 0;
}

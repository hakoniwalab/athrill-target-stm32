/*
 * Unit tests of the GPIO and SPI models with the MS5611 chip model
 * (src/device/peripheral/stm32_gpio.c, stm32_spi.c, src/device/sensor/ms5611.c),
 * driven as NuttX's polled SPI and PX4's ms5611 driver do. The sensor source
 * is stubbed.
 */
#include <stdio.h>
#include "peripheral/stm32_gpio.h"
#include "peripheral/stm32_spi.h"
#include "sensor/ms5611.h"
#include "sensor/sensor_source.h"

static float32 source_pressure = 1013.25F;
static float32 source_temperature = 20.0F;

void sensor_source_sample(SensorSampleType *sample)
{
	sample->abs_pressure = source_pressure;
	sample->temperature = source_temperature;
}

int intc_raise_intr(uint32 intno)
{
	(void)intno;
	return 0;
}

static int failures;
static int checks;

#define CHECK_EQ(what, got, want) check_eq(__LINE__, what, (long long)(got), (long long)(want))

static void check_eq(int line, const char *what, long long got, long long want)
{
	checks++;
	if (got != want) {
		failures++;
		printf("FAIL line %d: %s = %lld (0x%llx), want %lld (0x%llx)\n", line, what, got, got, want, want);
	}
}

#define GPIOF	0x40021400U
#define SPI4	0x40013400U
#define MODER	0x00U
#define BSRR	0x18U
#define IDR	0x10U
#define CR1	0x00U
#define CR2	0x04U
#define SR	0x08U
#define DR	0x0CU

static Ms5611Type baro;

static void cs(std_bool low)
{
	stm32_gpio_write(GPIOF + BSRR, 4U, low ? (1U << (10U + 16U)) : (1U << 10U));
}

/* NuttX spi_send for 8-bit frames: wait TXE, write a byte, wait RXNE, read a byte. */
static uint8 send(uint8 b)
{
	if ((stm32_spi_read(SPI4 + SR, 4U) & 2U) == 0U) {
		return 0xEEU;
	}
	stm32_spi_write(SPI4 + DR, 1U, b);
	if ((stm32_spi_read(SPI4 + SR, 4U) & 1U) == 0U) {
		return 0xEEU;
	}
	return (uint8)stm32_spi_read(SPI4 + DR, 1U);
}

static uint16 prom(uint32 i)
{
	uint16 w;

	cs(TRUE);
	(void)send((uint8)(0xA0U + (2U * i)));
	w = (uint16)(send(0U) << 8);
	w |= send(0U);
	cs(FALSE);
	return w;
}

static uint32 convert_and_read(uint8 cmd)
{
	uint32 v;

	cs(TRUE);
	(void)send(cmd);
	cs(FALSE);
	cs(TRUE);
	(void)send(0x00U);
	v = (uint32)send(0U) << 16;
	v |= (uint32)send(0U) << 8;
	v |= send(0U);
	cs(FALSE);
	return v;
}

/* PX4's ms5611::crc4 */
static int crc4_ok(uint16 *n_prom)
{
	int cnt;
	uint16 n_rem = 0U;
	uint16 crc_read = n_prom[7];
	int n_bit;

	n_prom[7] = (uint16)(0xFF00U & n_prom[7]);
	for (cnt = 0; cnt < 16; cnt++) {
		if (cnt & 1) {
			n_rem ^= (uint8)(n_prom[cnt >> 1] & 0x00FFU);
		}
		else {
			n_rem ^= (uint8)(n_prom[cnt >> 1] >> 8);
		}
		for (n_bit = 8; n_bit > 0; n_bit--) {
			n_rem = (n_rem & 0x8000U) ? (uint16)((n_rem << 1) ^ 0x3000U) : (uint16)(n_rem << 1);
		}
	}
	n_rem = (uint16)(0x000FU & (n_rem >> 12));
	n_prom[7] = crc_read;
	return (0x000FU & crc_read) == n_rem;
}

static void setup(void)
{
	stm32_gpio_reset();
	stm32_spi_reset();
	ms5611_reset(&baro);
	stm32_spi_attach(4U, STM32_GPIO_PORT('F'), 10U, &ms5611_ops, &baro);
	stm32_gpio_write(GPIOF + MODER, 4U, 1U << 20);	/* PF10 output */
	cs(FALSE);
	stm32_spi_write(SPI4 + CR2, 4U, (7U << 8) | (1U << 12));	/* 8-bit, FRXTH */
	stm32_spi_write(SPI4 + CR1, 4U, (1U << 6) | (1U << 2));		/* SPE, MSTR */
}

static void test_gpio(void)
{
	stm32_gpio_reset();
	stm32_gpio_write(GPIOF + MODER, 4U, 1U << 20);
	stm32_gpio_write(GPIOF + BSRR, 4U, (1U << 10) | (1U << 3));
	CHECK_EQ("BSRR set, output pin in IDR", stm32_gpio_read(GPIOF + IDR) & ((1U << 10) | (1U << 3)), 1U << 10);
	stm32_gpio_drive(STM32_GPIO_PORT('F'), 3U, TRUE);
	CHECK_EQ("driven input pin in IDR", stm32_gpio_read(GPIOF + IDR) & (1U << 3), 1U << 3);
	stm32_gpio_write(GPIOF + BSRR, 4U, (1U << 26) | (1U << 10));
	CHECK_EQ("BS wins over BR", stm32_gpio_output(STM32_GPIO_PORT('F'), 10U), TRUE);
	stm32_gpio_write(GPIOF + BSRR, 4U, 1U << 26);
	CHECK_EQ("BR resets", stm32_gpio_output(STM32_GPIO_PORT('F'), 10U), FALSE);
	CHECK_EQ("GPIOA MODER reset value", stm32_gpio_read(0x40020000U + MODER), 0xA8000000U);
}

static void test_spi(void)
{
	setup();
	CHECK_EQ("SR after reset: TXE", stm32_spi_read(SPI4 + SR, 4U) & 3U, 2U);
	CHECK_EQ("no chip selected: MISO high", send(0x12U), 0xFFU);
	/* FRXTH = 0: RXNE needs two bytes */
	stm32_spi_write(SPI4 + CR2, 4U, 7U << 8);
	stm32_spi_write(SPI4 + DR, 1U, 0x00U);
	CHECK_EQ("FRXTH 0, one byte: no RXNE", stm32_spi_read(SPI4 + SR, 4U) & 1U, 0U);
	CHECK_EQ("FRLVL quarter", (stm32_spi_read(SPI4 + SR, 4U) >> 9) & 3U, 1U);
	stm32_spi_write(SPI4 + DR, 1U, 0x00U);
	CHECK_EQ("two bytes: RXNE", stm32_spi_read(SPI4 + SR, 4U) & 1U, 1U);
	CHECK_EQ("16-bit read takes both", stm32_spi_read(SPI4 + DR, 2U), 0xFFFFU);
	CHECK_EQ("FIFO empty", (stm32_spi_read(SPI4 + SR, 4U) >> 9) & 3U, 0U);
	/* SPE cleared: nothing sent */
	stm32_spi_write(SPI4 + CR1, 4U, 1U << 2);
	stm32_spi_write(SPI4 + DR, 1U, 0x00U);
	CHECK_EQ("SPE 0: nothing received", stm32_spi_read(SPI4 + SR, 4U) & 1U, 0U);
}

static void test_ms5611(void)
{
	uint16 words[8];
	uint32 i;
	uint32 d1;
	uint32 d2;
	sint32 p;
	sint32 t;
	float32 temps[] = { 20.0F, 35.5F, 5.0F, -20.0F };
	float32 pressures[] = { 1013.25F, 850.0F, 1100.0F, 1013.25F };

	setup();
	for (i = 0U; i < 8U; i++) {
		words[i] = prom(i);
	}
	CHECK_EQ("PROM C1 over SPI", words[1], 40127U);
	CHECK_EQ("PROM C6 over SPI", words[6], 28312U);
	CHECK_EQ("PROM CRC as PX4 checks it", crc4_ok(words), 1);
	/* raw values that give the sample back through PX4's compensation, within 0.01 */
	for (i = 0U; i < 4U; i++) {
		source_pressure = pressures[i];
		source_temperature = temps[i];
		d2 = convert_and_read(0x58U);
		d1 = convert_and_read(0x48U);
		ms5611_compensate(&baro, d1, d2, &p, &t);
		CHECK_EQ("temperature (0.01 C)", t, (sint32)(temps[i] * 100.0F));
		CHECK_EQ("pressure (0.01 hPa)", p, (sint32)(pressures[i] * 100.0F));
	}
	/* a new transaction restarts the command */
	cs(TRUE);
	(void)send(0xA2U);
	cs(FALSE);
	CHECK_EQ("PROM read restarts after CS", prom(1U), 40127U);
}

int main(void)
{
	test_gpio();
	test_spi();
	test_ms5611();
	printf("%d checks, %d failures\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}

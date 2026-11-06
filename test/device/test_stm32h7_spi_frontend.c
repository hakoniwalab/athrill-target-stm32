/*
 * Register-front-end tests for the STM32H743 SPI v2 block. Expectations are
 * from RM0433 Rev 8 sections 50.4.13 (data packing), 50.4.14 (DMA), and
 * 50.11.1-50.11.9 (CR1, CR2/TSIZE, CFG1, CFG2, SR, IFCR, TXDR and RXDR).
 *
 * This executable is marked WILL_FAIL until the front end implements the
 * reference-manual state machine. It deliberately accumulates every failure
 * so one run documents all unsupported driver-visible behavior.
 */
#include <stdio.h>
#include "peripheral/stm32h7_spi.h"
#include "peripheral/stm32_spi.h"

#define SPI2		0x40003800U
#define CR1		0x00U
#define CR2		0x04U
#define CFG1		0x08U
#define CFG2		0x0CU
#define SR		0x14U
#define IFCR		0x18U
#define TXDR		0x20U
#define RXDR		0x30U

#define SPE		(1U << 0)
#define CSTART		(1U << 9)
#define RXDMAEN		(1U << 14)
#define TXDMAEN		(1U << 15)
#define MASTER		(1U << 22)
#define CPHA		(1U << 24)
#define CPOL		(1U << 25)
#define SSM		(1U << 26)
#define RXP		(1U << 0)
#define TXP		(1U << 1)
#define EOT		(1U << 3)
#define SUSP		(1U << 11)
#define TXC		(1U << 12)
#define EOTC		(1U << 3)

#define CFG1_RESET	0x00070007U

static int checks;
static int failures;
static uint32 raised;

#define CHECK_EQ(what, got, want) \
	check_eq(__LINE__, what, (unsigned long long)(got), (unsigned long long)(want))

static void check_eq(int line, const char *what, unsigned long long got,
	unsigned long long want)
{
	checks++;
	if (got != want) {
		failures++;
		printf("FAIL line %d: %s = 0x%llx, want 0x%llx\n",
			line, what, got, want);
	}
}

int intc_raise_intr(uint32 intno)
{
	(void)intno;
	raised++;
	return 0;
}

/* Deterministic full-duplex peer: each received byte is the MOSI byte XOR FF. */
uint8 stm32_spi_chip_exchange(uint32 bus, uint8 mosi)
{
	CHECK_EQ("SPI2 backend bus", bus, 2U);
	return (uint8)(mosi ^ 0xFFU);
}

static uint32 rd(uint32 offset)
{
	return stm32h7_spi_read(SPI2 + offset, 4U);
}

static void wr(uint32 offset, uint32 value)
{
	stm32h7_spi_write(SPI2 + offset, 4U, value);
}

static void test_register_file(void)
{
	const uint32 cfg1 = (3U << 28) | (3U << 5) | 7U;
	const uint32 cfg2 = MASTER | SSM | CPOL | CPHA;

	stm32h7_spi_reset();
	CHECK_EQ("CR1 reset", rd(CR1), 0U);
	CHECK_EQ("CFG1 reset", rd(CFG1), CFG1_RESET);
	CHECK_EQ("CFG2 reset", rd(CFG2), 0U);
	CHECK_EQ("SR reset", rd(SR), TXC | TXP);

	/* RM0433 50.11.1-50.11.4: configuration is writable with SPE=0. */
	wr(CR2, 2U);
	wr(CFG1, cfg1);
	wr(CFG2, cfg2);
	CHECK_EQ("TSIZE readback while disabled", rd(CR2) & 0xFFFFU, 2U);
	CHECK_EQ("CFG1 readback while disabled", rd(CFG1), cfg1);
	CHECK_EQ("CFG2 readback while disabled", rd(CFG2), cfg2);

	/* With SPE=1, CFG2 and non-DMA CFG1 fields are write protected. */
	wr(CR1, SPE);
	wr(CFG2, 0U);
	CHECK_EQ("CFG2 write protected while enabled", rd(CFG2), cfg2);
	wr(CFG1, TXDMAEN | RXDMAEN | 7U);
	CHECK_EQ("only CFG1 DMA enables change while enabled", rd(CFG1),
		cfg1 | TXDMAEN | RXDMAEN);
}

static void test_tsize_and_flags(void)
{
	uint32 status;

	stm32h7_spi_reset();
	wr(CR2, 2U); /* two 8-bit frames */
	wr(CR1, SPE | CSTART);
	status = rd(SR);
	CHECK_EQ("start: CTSIZE", status >> 16, 2U);
	CHECK_EQ("start: TXP only", status & (TXP | RXP | EOT | SUSP | TXC), TXP);

	stm32h7_spi_write(SPI2 + TXDR, 1U, 0x11U);
	status = rd(SR);
	CHECK_EQ("one frame: CTSIZE", status >> 16, 1U);
	CHECK_EQ("one frame: RXP without EOT", status & (RXP | EOT), RXP);
	CHECK_EQ("first RX byte", stm32h7_spi_read(SPI2 + RXDR, 1U), 0xEEU);
	CHECK_EQ("read drains RXP", rd(SR) & RXP, 0U);

	stm32h7_spi_write(SPI2 + TXDR, 1U, 0x22U);
	status = rd(SR);
	CHECK_EQ("complete: CTSIZE", status >> 16, 0U);
	CHECK_EQ("complete: RXP EOT TXC", status & (RXP | EOT | TXC),
		RXP | EOT | TXC);
	CHECK_EQ("EOT clears CSTART", rd(CR1) & CSTART, 0U);
	wr(IFCR, EOTC);
	CHECK_EQ("EOTC clears EOT and TSIZE-mode TXC", rd(SR) & (EOT | TXC), 0U);
}

static void test_fifo_threshold(void)
{
	uint32 packed;

	stm32h7_spi_reset();
	/* FTHLV=1 encodes a two-frame packet; DSIZE=7 encodes 8-bit frames. */
	wr(CFG1, CFG1_RESET | (1U << 5));
	wr(CR2, 2U);
	wr(CR1, SPE | CSTART);
	stm32h7_spi_write(SPI2 + TXDR, 1U, 0x31U);
	CHECK_EQ("one frame is below two-frame RXP threshold", rd(SR) & RXP, 0U);
	stm32h7_spi_write(SPI2 + TXDR, 1U, 0x42U);
	CHECK_EQ("two frames assert RXP", rd(SR) & RXP, RXP);
	packed = stm32h7_spi_read(SPI2 + RXDR, 2U);
	CHECK_EQ("packed RX order", packed, 0xBDCEU);
	CHECK_EQ("packed read clears RXP", rd(SR) & RXP, 0U);
}

static void test_dma_requests(void)
{
	uint32 rxdr = 0U;
	uint32 response = 0U;

	stm32h7_spi_reset();
	wr(CR1, SPE);
	/* RM0433 50.4.14: no TX DMA request exists while TXDMAEN is clear. */
	CHECK_EQ("TX DMA disabled: no request", stm32h7_spi_dma_exchange(
		SPI2 + TXDR, 1U, 0x5AU, &rxdr, &response), FALSE);

	stm32h7_spi_reset();
	wr(CFG1, CFG1_RESET | TXDMAEN | RXDMAEN);
	wr(CR1, SPE);
	CHECK_EQ("TXP with TXDMAEN produces request", stm32h7_spi_dma_exchange(
		SPI2 + TXDR, 1U, 0x5AU, &rxdr, &response), TRUE);
	CHECK_EQ("paired RX DMA address", rxdr, SPI2 + RXDR);
	CHECK_EQ("paired RX DMA datum", response, 0xA5U);
}

int main(void)
{
	test_register_file();
	test_tsize_and_flags();
	test_fifo_threshold();
	test_dma_requests();
	printf("%d checks, %d failures\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}

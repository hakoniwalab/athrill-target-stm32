/*
 * Unit tests of the SDMMC1 controller with the SDHC card model
 * (src/device/peripheral/stm32_sdmmc.c, src/device/memory/sdcard.c), driven
 * as NuttX's STM32F7 SDMMC and MMC/SD drivers do: the identification
 * sequence, the SCR through the FIFO, block reads and writes by DMA (DMA2
 * stream 6, peripheral flow control) and the time a transfer takes. The
 * CPU clock, the NVIC, the bus (memory) and the SPI are stubbed.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "peripheral/stm32_sdmmc.h"
#include "peripheral/stm32_dma.h"
#include "memory/sdcard.h"
#include "bus.h"

/* stubs */
static uint64 now;
static uint32 raised[128];
static uint8 active[128];
static uint8 ram[0x2000];
#define RAM_BASE	0x20000000U

uint64 cpuemu_get_clock(void)
{
	return now;
}

int intc_raise_intr(uint32 intno)
{
	raised[intno]++;
	return 0;
}

std_bool intc_irq_active(uint32 intno)
{
	return active[intno] != 0U;
}

std_bool stm32_spi_owns(uint32 addr)
{
	(void)addr;
	return FALSE;
}

uint32 stm32_spi_read(uint32 addr, uint32 size)
{
	(void)addr;
	(void)size;
	return 0U;
}

void stm32_spi_write(uint32 addr, uint32 size, uint32 value)
{
	(void)addr;
	(void)size;
	(void)value;
}

Std_ReturnType mpu_get_data8(CoreIdType c, AthrillAddrType a, uint8 *d)
{
	(void)c;
	*d = ram[a - RAM_BASE];
	return STD_E_OK;
}

Std_ReturnType mpu_get_data16(CoreIdType c, AthrillAddrType a, uint16 *d)
{
	(void)c;
	memcpy(d, &ram[a - RAM_BASE], 2U);
	return STD_E_OK;
}

Std_ReturnType mpu_get_data32(CoreIdType c, AthrillAddrType a, uint32 *d)
{
	(void)c;
	memcpy(d, &ram[a - RAM_BASE], 4U);
	return STD_E_OK;
}

Std_ReturnType mpu_put_data8(CoreIdType c, AthrillAddrType a, uint8 d)
{
	(void)c;
	ram[a - RAM_BASE] = d;
	return STD_E_OK;
}

Std_ReturnType mpu_put_data16(CoreIdType c, AthrillAddrType a, uint16 d)
{
	(void)c;
	memcpy(&ram[a - RAM_BASE], &d, 2U);
	return STD_E_OK;
}

Std_ReturnType mpu_put_data32(CoreIdType c, AthrillAddrType a, uint32 d)
{
	(void)c;
	memcpy(&ram[a - RAM_BASE], &d, 4U);
	return STD_E_OK;
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

#define SD	0x40012C00U
#define CLKCR	0x04U
#define ARG	0x08U
#define CMD	0x0CU
#define RESPCMD	0x10U
#define RESP1	0x14U
#define RESP2	0x18U
#define DLEN	0x28U
#define DCTRL	0x2CU
#define STA	0x34U
#define ICR	0x38U
#define MASK	0x3CU
#define FIFO	0x80U
#define DMA2	0x40026400U
#define S6	(0x10U + (6U * 0x18U))	/* DMA2 stream 6 */
#define IRQ	49U

#define CTIMEOUT	(1U << 2)
#define CMDREND		(1U << 6)
#define CMDSENT		(1U << 7)
#define DATAEND		(1U << 8)
#define RXDAVL		(1U << 21)

#define CPU_HZ		216000000U
#define IMAGE		"test_sdmmc_sdcard.img"

static SdcardType card = { .fd = -1 };

static void wr(uint32 offset, uint32 value)
{
	stm32_sdmmc_write(SD + offset, 4U, value);
}

static uint32 rd(uint32 offset)
{
	return stm32_sdmmc_read(SD + offset);
}

static void tick(void)
{
	DeviceClockType clock;

	clock.clock = now;
	clock.min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	stm32_sdmmc_tick(&clock);
}

/* NuttX stm32_sendcmd: WAITRESP 0 none, 1 short, 3 long; returns STA. */
static uint32 command(uint32 index, uint32 arg, uint32 wait)
{
	wr(ICR, 0x004005FFU);
	wr(ARG, arg);
	wr(CMD, index | (wait << 6) | (1U << 10));
	return rd(STA);
}

static void setup(void)
{
	FILE *f = fopen(IMAGE, "wb");
	uint8 block[512];
	uint32 i;

	/* 1 MiB image, block n starts with n */
	for (i = 0U; i < 2048U; i++) {
		memset(block, (int)(i & 0xFFU), sizeof(block));
		fwrite(block, 1U, sizeof(block), f);
	}
	fclose(f);
	now = 0U;
	memset(raised, 0, sizeof(raised));
	stm32_sdmmc_reset(CPU_HZ);
	stm32_dma_reset();
	sdcard_open(&card, IMAGE);
	stm32_sdmmc_attach(&card);
}

static void test_identify(void)
{
	uint32 rca;

	setup();
	CHECK_EQ("CMD0: sent", command(0U, 0U, 0U) & CMDSENT, CMDSENT);
	CHECK_EQ("CMD5 (SDIO probe): timeout", command(5U, 0U, 1U) & CTIMEOUT, CTIMEOUT);
	CHECK_EQ("CMD8: response", command(8U, 0x1AAU, 1U) & CMDREND, CMDREND);
	CHECK_EQ("CMD8: echo", rd(RESP1), 0x1AAU);
	CHECK_EQ("CMD8: RESPCMD", rd(RESPCMD), 8U);
	(void)command(55U, 0U, 1U);
	CHECK_EQ("CMD55: APP_CMD", rd(RESP1) & (1U << 5), 1U << 5);
	(void)command(41U, 0x40100000U, 1U);
	CHECK_EQ("ACMD41: ready, high capacity", rd(RESP1) & 0xC0000000U, 0xC0000000U);
	CHECK_EQ("ACMD41: RESPCMD 0x3F (R3)", rd(RESPCMD), 0x3FU);
	CHECK_EQ("CMD2: long response", command(2U, 0U, 3U) & CMDREND, CMDREND);
	(void)command(3U, 0U, 1U);
	rca = rd(RESP1) >> 16;
	CHECK_EQ("CMD3: RCA", rca, 0x1234U);
	(void)command(9U, rca << 16, 3U);
	CHECK_EQ("CSD 2.0", rd(RESP1) >> 30, 1U);
	/* C_SIZE bits 69..48: RESP2 [5:0] and RESP3 [31:16]; 1 MiB = 2 x 512 KiB */
	CHECK_EQ("C_SIZE", ((rd(RESP2) & 0x3FU) << 16) | (stm32_sdmmc_read(SD + 0x1CU) >> 16), 1U);
	(void)command(7U, rca << 16, 1U);
	(void)command(13U, rca << 16, 1U);
	CHECK_EQ("CMD13: transfer state", (rd(RESP1) >> 9) & 15U, 4U);
}

static void test_scr_fifo(void)
{
	uint32 w0;

	test_identify();
	/* NuttX recvsetup: DLEN 8, 8-byte blocks, card to controller, no DMA; then CMD55 + ACMD51 */
	wr(DLEN, 8U);
	wr(DCTRL, (3U << 4) | 3U);
	wr(MASK, (1U << 8) | (1U << 15));	/* DATAEND, RXFIFOHF */
	(void)command(55U, 0x1234U << 16, 1U);
	(void)command(51U, 0U, 1U);
	CHECK_EQ("ACMD51: no data yet", rd(STA) & DATAEND, 0U);
	now = 100000U;
	tick();
	CHECK_EQ("DATAEND, data in the FIFO", rd(STA) & (DATAEND | RXDAVL), DATAEND | RXDAVL);
	CHECK_EQ("interrupt", raised[IRQ] != 0U, 1);
	w0 = rd(FIFO);
	CHECK_EQ("SCR: SD 2.0 (byte 0 low), 4-bit bus", w0 & 0x0F0FU, 0x0502U);
	(void)rd(FIFO);
	CHECK_EQ("FIFO empty", rd(STA) & RXDAVL, 0U);
}

/* NuttX dmarecvsetup / dmasendsetup: the stream of the FIFO, 32-bit, PFCTRL, MINC, TCIE */
static void dma_stream(uint32 memory, std_bool to_memory)
{
	uint32 cr = (1U << 5) | (1U << 10) | (2U << 11) | (2U << 13) | (1U << 4) | (4U << 25);

	cr |= to_memory ? 0U : (1U << 6);
	stm32_dma_write(DMA2 + S6 + 0x08U, 4U, SD + FIFO);
	stm32_dma_write(DMA2 + S6 + 0x0CU, 4U, memory);
	stm32_dma_write(DMA2 + S6 + 0x04U, 4U, 128U);
	stm32_dma_write(DMA2 + S6, 4U, cr | 1U);
}

static void test_dma_blocks(void)
{
	uint64 start;

	test_identify();
	wr(CLKCR, (1U << 11) | 1U);	/* 4-bit bus, 48 MHz / 3 = 16 MHz */
	wr(MASK, 1U << 8);
	/* read block 3: data path and stream before CMD17 */
	wr(DLEN, 512U);
	wr(DCTRL, (9U << 4) | 3U | (1U << 3));
	dma_stream(RAM_BASE + 0x100U, TRUE);
	start = now;
	(void)command(17U, 3U, 1U);
	now = start + 13000U;
	tick();
	CHECK_EQ("read: not before the bus time", rd(STA) & DATAEND, 0U);
	/* 512 bytes on 4 bits = 1024 cycles + 100 at 16 MHz = 70.3 us = 15187 CPU clocks */
	now = start + 15188U;
	tick();
	CHECK_EQ("read: DATAEND", rd(STA) & DATAEND, DATAEND);
	CHECK_EQ("read: block 3 in memory", ram[0x100] | (ram[0x2FF] << 8), 0x0303U);
	CHECK_EQ("read: DMA TCIF6 (HISR bit 21)", stm32_dma_read(DMA2 + 0x04U) & (1U << 21), 1U << 21);
	/* write block 5 from memory: CMD24, then the data path and the stream */
	memset(&ram[0x400], 0xA5, 512U);
	wr(ICR, 0x004005FFU);
	stm32_dma_write(DMA2 + 0x0CU, 4U, 0xFFFFFFFFU);
	(void)command(24U, 5U, 1U);
	wr(DLEN, 512U);
	wr(DCTRL, (9U << 4) | 1U | (1U << 3));
	dma_stream(RAM_BASE + 0x400U, FALSE);
	start = now;
	wr(MASK, 1U << 8);	/* NuttX configxfrints after dmastart: the transfer starts */
	now = start + 15000U;
	tick();
	CHECK_EQ("write: not before the bus time", rd(STA) & DATAEND, 0U);
	now = start + 15188U;
	tick();
	CHECK_EQ("write: DATAEND", rd(STA) & DATAEND, DATAEND);
	/* read it back */
	wr(ICR, 0x004005FFU);
	wr(DLEN, 512U);
	wr(DCTRL, (9U << 4) | 3U | (1U << 3));
	dma_stream(RAM_BASE + 0x800U, TRUE);
	(void)command(17U, 5U, 1U);
	now += 20000U;
	tick();
	CHECK_EQ("written block read back", ram[0x800] | (ram[0x9FF] << 8), 0xA5A5U);
}

int main(void)
{
	test_identify();
	test_scr_fifo();
	test_dma_blocks();
	sdcard_open(&card, NULL);
	unlink(IMAGE);
	printf("%d checks, %d failures\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}

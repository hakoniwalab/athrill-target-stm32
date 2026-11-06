/*
 * STM32H7 SDMMC2 v2 tests: card identification and block transfers through
 * internal DMA, using the ordering of the NuttX H7 SDMMC driver.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "peripheral/stm32h7_sdmmc.h"
#include "memory/sdcard.h"
#include "bus.h"

#define SD		0x48022400U
#define CLKCR		0x04U
#define ARG		0x08U
#define CMD		0x0CU
#define RESPCMD		0x10U
#define RESP1		0x14U
#define DLEN		0x28U
#define DCTRL		0x2CU
#define STA		0x34U
#define ICR		0x38U
#define MASK		0x3CU
#define IDMACTRL	0x50U
#define IDMABASE0	0x58U
#define IRQ		124U
#define RAM_BASE	0x24000000U
#define CPU_HZ		480000000U
#define IMAGE		"test_stm32h7_sdmmc.img"

#define CTIMEOUT	(1U << 2)
#define CMDREND		(1U << 6)
#define CMDSENT		(1U << 7)
#define DATAEND		(1U << 8)
#define DPSMACT		(1U << 12)

static uint64 now;
static uint32 raised[150];
static uint8 active[150];
static uint8 ram[0x2000];
static int failures;
static int checks;
static SdcardType card = { .fd = -1 };

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

static void check_eq(int line, const char *what, long long got, long long want)
{
	checks++;
	if (got != want) {
		failures++;
		printf("FAIL line %d: %s = %lld (0x%llx), want %lld (0x%llx)\n",
			line, what, got, got, want, want);
	}
}

#define CHECK_EQ(what, got, want) check_eq(__LINE__, what, (long long)(got), (long long)(want))

static void wr(uint32 offset, uint32 value)
{
	stm32h7_sdmmc_write(SD + offset, 4U, value);
}

static uint32 rd(uint32 offset)
{
	return stm32h7_sdmmc_read(SD + offset);
}

static void tick(void)
{
	DeviceClockType clock;

	clock.clock = now;
	clock.min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	stm32h7_sdmmc_tick(&clock);
}

static uint32 command(uint32 index, uint32 arg, uint32 wait, std_bool data)
{
	wr(ICR, 0x1FE00FFFU);
	wr(ARG, arg);
	wr(CMD, index | (data ? (1U << 6) : 0U) | (wait << 8) | (1U << 12));
	return rd(STA);
}

static void setup(void)
{
	FILE *f = fopen(IMAGE, "wb");
	uint8 block[512];
	uint32 i;

	for (i = 0U; i < 2048U; i++) {
		memset(block, (int)(i & 0xFFU), sizeof(block));
		(void)fwrite(block, 1U, sizeof(block), f);
	}
	(void)fclose(f);
	memset(ram, 0, sizeof(ram));
	memset(raised, 0, sizeof(raised));
	now = 0U;
	stm32h7_sdmmc_reset(CPU_HZ);
	sdcard_open(&card, IMAGE);
	stm32h7_sdmmc_attach(&card);
}

static void identify(void)
{
	uint32 rca;

	CHECK_EQ("owns SDMMC2", stm32h7_sdmmc_owns(SD), TRUE);
	CHECK_EQ("not SDMMC1", stm32h7_sdmmc_owns(0x52007000U), FALSE);
	CHECK_EQ("CMD0 sent", command(0U, 0U, 0U, FALSE) & CMDSENT, CMDSENT);
	CHECK_EQ("CMD5 timeout", command(5U, 0U, 1U, FALSE) & CTIMEOUT, CTIMEOUT);
	CHECK_EQ("CMD8 response", command(8U, 0x1AAU, 1U, FALSE) & CMDREND, CMDREND);
	CHECK_EQ("CMD8 echo", rd(RESP1), 0x1AAU);
	CHECK_EQ("CMD8 index", rd(RESPCMD), 8U);
	(void)command(55U, 0U, 1U, FALSE);
	(void)command(41U, 0x40100000U, 1U, FALSE);
	CHECK_EQ("ACMD41 ready", rd(RESP1) & 0xC0000000U, 0xC0000000U);
	(void)command(2U, 0U, 3U, FALSE);
	(void)command(3U, 0U, 1U, FALSE);
	rca = rd(RESP1) >> 16;
	CHECK_EQ("RCA", rca, 0x1234U);
	(void)command(7U, rca << 16, 1U, FALSE);
}

static void idma(uint32 address, std_bool read)
{
	wr(IDMACTRL, 0U);
	wr(DLEN, 512U);
	wr(DCTRL, (9U << 4) | (read ? (1U << 1) : 0U));
	wr(IDMABASE0, address);
	wr(IDMACTRL, 1U);
	wr(MASK, DATAEND);
}

static void test_idma_blocks(void)
{
	uint64 start;

	setup();
	identify();
	wr(CLKCR, (1U << 14) | 5U); /* four bits, PLL1Q 240 MHz / (2 * 5) = 24 MHz */
	idma(RAM_BASE + 0x100U, TRUE);
	start = now;
	(void)command(17U, 3U, 1U, TRUE);
	CHECK_EQ("read active", rd(STA) & DPSMACT, DPSMACT);
	now = start + 22000U;
	tick();
	CHECK_EQ("not before bus time", rd(STA) & DATAEND, 0U);
	now = start + 22500U;
	tick();
	CHECK_EQ("read DATAEND", rd(STA) & DATAEND, DATAEND);
	CHECK_EQ("read block 3", ram[0x100U] | (ram[0x2FFU] << 8), 0x0303U);
	CHECK_EQ("SDMMC2 IRQ", raised[IRQ] != 0U, TRUE);

	memset(&ram[0x400U], 0xA5, 512U);
	idma(RAM_BASE + 0x400U, FALSE);
	start = now;
	(void)command(24U, 5U, 1U, TRUE);
	now = start + 22500U;
	tick();
	CHECK_EQ("write DATAEND", rd(STA) & DATAEND, DATAEND);

	idma(RAM_BASE + 0x800U, TRUE);
	(void)command(17U, 5U, 1U, TRUE);
	now += 22500U;
	tick();
	CHECK_EQ("written block read back", ram[0x800U] | (ram[0x9FFU] << 8), 0xA5A5U);
}

int main(void)
{
	test_idma_blocks();
	sdcard_open(&card, NULL);
	(void)unlink(IMAGE);
	printf("%d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}

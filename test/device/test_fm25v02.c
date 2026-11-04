/*
 * Unit tests of the FM25V02A F-RAM model (src/device/memory/fm25v02.c),
 * driven as NuttX's ramtron driver does: RDID, WREN + WRITE, READ, and the
 * contents kept in a host file across a reopen.
 */
#include <stdio.h>
#include <string.h>
#include "memory/fm25v02.h"

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

static Fm25v02Type fram = { .fd = -1 };

/* One transaction: chip select, the bytes out, the bytes back into in (if any), deselect. */
static void transfer(const uint8 *out, uint8 *in, uint32 n)
{
	uint32 i;

	fm25v02_ops.select(&fram, TRUE);
	for (i = 0U; i < n; i++) {
		uint8 b = fm25v02_ops.exchange(&fram, out[i]);

		if (in != NULL) {
			in[i] = b;
		}
	}
	fm25v02_ops.select(&fram, FALSE);
}

int main(void)
{
	const char *path = "test_fm25v02.bin";
	uint8 rdid[10] = { 0x9FU, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U, 0xA5U };
	uint8 id[10];
	uint8 wren = 0x06U;
	uint8 rdsr[2] = { 0x05U, 0U };
	uint8 sr[2];
	uint8 write[6] = { 0x02U, 0x7FU, 0xFEU, 0x11U, 0x22U, 0x33U };	/* wraps at the end */
	uint8 read[5] = { 0x03U, 0x7FU, 0xFEU, 0U, 0U };
	uint8 back[5];
	uint8 read0[4] = { 0x03U, 0x00U, 0x00U, 0U };
	uint8 back0[4];

	remove(path);
	fm25v02_open(&fram, path);
	transfer(rdid, id, 10U);
	CHECK_EQ("RDID continuation codes", id[1] & id[6], 0x7FU);
	CHECK_EQ("RDID manufacturer", id[7], 0xC2U);
	CHECK_EQ("RDID density (ramtron id1)", id[8], 0x22U);
	CHECK_EQ("RDID FM25V02A (ramtron id2)", id[9], 0x08U);
	/* no WREN: the write is ignored */
	transfer(write, NULL, 6U);
	transfer(read, back, 5U);
	CHECK_EQ("write without WREN ignored", back[3], 0U);
	transfer(&wren, NULL, 1U);
	transfer(rdsr, sr, 2U);
	CHECK_EQ("WREN: WEL", sr[1] & 0x02U, 0x02U);
	transfer(write, NULL, 6U);
	transfer(rdsr, sr, 2U);
	CHECK_EQ("end of WRITE clears WEL", sr[1] & 0x02U, 0U);
	transfer(read, back, 5U);
	CHECK_EQ("read 0x7FFE", back[3], 0x11U);
	CHECK_EQ("read 0x7FFF", back[4], 0x22U);
	transfer(read0, back0, 4U);
	CHECK_EQ("address wraps to 0", back0[3], 0x33U);
	/* the host file keeps the contents */
	fm25v02_open(&fram, path);
	transfer(read, back, 5U);
	CHECK_EQ("reopened: 0x7FFE", back[3], 0x11U);
	transfer(read0, back0, 4U);
	CHECK_EQ("reopened: 0", back0[3], 0x33U);
	fm25v02_open(&fram, NULL);
	remove(path);
	printf("%d checks, %d failures\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}

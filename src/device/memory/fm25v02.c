/*
 * The FM25V02A F-RAM (see fm25v02.h).
 */
#include "fm25v02.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CMD_WRSR	0x01U
#define CMD_WRITE	0x02U
#define CMD_READ	0x03U
#define CMD_WRDI	0x04U
#define CMD_RDSR	0x05U
#define CMD_WREN	0x06U
#define CMD_FSTRD	0x0BU
#define CMD_RDID	0x9FU

#define SR_WEL		(1U << 1)
#define SR_WRITABLE	0x8CU	/* WPEN, BP1, BP0 */

/* RDID: six continuation codes 0x7F, Cypress (0xC2), density 256 Kbit (0x22), FM25V02A (0x08). */
static const uint8 device_id[9] = { 0x7FU, 0x7FU, 0x7FU, 0x7FU, 0x7FU, 0x7FU, 0xC2U, 0x22U, 0x08U };

/* The bytes written since the last update go to the host file. */
static void save(Fm25v02Type *chip)
{
	uint32 lo = chip->dirty_lo;
	uint32 hi = chip->dirty_hi;

	chip->dirty_lo = FM25V02_SIZE;
	chip->dirty_hi = 0U;
	if ((chip->fd < 0) || (lo >= hi)) {
		return;
	}
	if (pwrite(chip->fd, &chip->data[lo], hi - lo, (off_t)lo) != (ssize_t)(hi - lo)) {
		printf("WARNING: can not write the F-RAM file\n");
	}
}

void fm25v02_open(Fm25v02Type *chip, const char *path)
{
	memset(chip->data, 0, sizeof(chip->data));
	chip->status = 0U;
	chip->command = 0U;
	chip->count = 0U;
	chip->address = 0U;
	chip->dirty_lo = FM25V02_SIZE;
	chip->dirty_hi = 0U;
	if (chip->fd >= 0) {
		close(chip->fd);
	}
	chip->fd = -1;
	if (path == NULL) {
		return;
	}
	chip->fd = open(path, O_RDWR | O_CREAT, 0644);
	if (chip->fd < 0) {
		printf("WARNING: can not open F-RAM file %s\n", path);
		return;
	}
	(void)pread(chip->fd, chip->data, FM25V02_SIZE, 0);
	if (ftruncate(chip->fd, FM25V02_SIZE) != 0) {
		printf("WARNING: can not size F-RAM file %s\n", path);
	}
}

static void select_chip(void *p, std_bool selected)
{
	Fm25v02Type *chip = (Fm25v02Type *)p;

	if (!selected) {
		/* a WRITE ends with the chip select: WEL is cleared, the file is updated */
		if ((chip->command == CMD_WRITE) || (chip->command == CMD_WRSR)) {
			chip->status &= (uint8)~SR_WEL;
		}
		save(chip);
	}
	chip->command = 0U;
	chip->count = 0U;
}

/* The bytes after a READ, FSTRD or WRITE command: the address, then the data. */
static uint8 data_phase(Fm25v02Type *chip, uint8 mosi)
{
	uint32 n = chip->count;
	uint32 first_data = (chip->command == CMD_FSTRD) ? 3U : 2U;	/* FSTRD has a dummy byte */
	uint8 miso = 0U;

	chip->count++;
	if (n < 2U) {
		chip->address = ((chip->address << 8) | mosi) & (FM25V02_SIZE - 1U);
		return 0U;
	}
	if (n < first_data) {
		return 0U;
	}
	if (chip->command == CMD_WRITE) {
		if ((chip->status & SR_WEL) != 0U) {
			chip->data[chip->address] = mosi;
			if (chip->address < chip->dirty_lo) {
				chip->dirty_lo = chip->address;
			}
			if (chip->address >= chip->dirty_hi) {
				chip->dirty_hi = chip->address + 1U;
			}
		}
	}
	else {
		miso = chip->data[chip->address];
	}
	chip->address = (chip->address + 1U) & (FM25V02_SIZE - 1U);
	return miso;
}

static uint8 exchange(void *p, uint8 mosi)
{
	Fm25v02Type *chip = (Fm25v02Type *)p;
	uint8 miso = 0U;

	if (chip->command == 0U) {
		chip->command = mosi;
		chip->count = 0U;
		chip->address = 0U;
		if (mosi == CMD_WREN) {
			chip->status |= SR_WEL;
		}
		else if (mosi == CMD_WRDI) {
			chip->status &= (uint8)~SR_WEL;
		}
		return 0U;
	}
	switch (chip->command) {
	case CMD_RDSR:
		miso = chip->status;
		break;
	case CMD_WRSR:
		if ((chip->count == 0U) && ((chip->status & SR_WEL) != 0U)) {
			chip->status = (uint8)((chip->status & ~SR_WRITABLE) | (mosi & SR_WRITABLE));
		}
		chip->count++;
		break;
	case CMD_RDID:
		miso = (chip->count < sizeof(device_id)) ? device_id[chip->count] : 0U;
		chip->count++;
		break;
	case CMD_READ:
	case CMD_FSTRD:
	case CMD_WRITE:
		miso = data_phase(chip, mosi);
		break;
	default:
		break;
	}
	return miso;
}

const Stm32SpiChipOps fm25v02_ops = { select_chip, exchange };

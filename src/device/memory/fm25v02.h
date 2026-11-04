/*
 * The FM25V02A 256-Kbit (32 KB) serial F-RAM on SPI (Cypress datasheet),
 * where Pixhawk boards keep the parameters: RDID, RDSR/WRSR, WREN/WRDI,
 * READ/FSTRD and WRITE with a 2-byte address that wraps at the end. Writes
 * take effect at once (F-RAM has no write delay). The contents can be kept
 * in a host file (fm25v02_open) so the parameters survive a restart of the
 * emulator, as they survive a power cycle on the board. Lightweight: no
 * block protection (BP bits are kept but not enforced), no sleep mode.
 */
#ifndef ATHRILL_STM32_FM25V02_H
#define ATHRILL_STM32_FM25V02_H

#include "std_types.h"
#include "peripheral/stm32_spi.h"

#define FM25V02_SIZE	32768U

typedef struct {
	uint8 data[FM25V02_SIZE];
	uint8 status;		/* WEL, BP, WPEN */
	uint8 command;		/* the first byte after chip select; 0: none yet */
	uint32 count;		/* bytes after the command */
	uint32 address;
	uint32 dirty_lo;	/* bytes written since the file was updated: [dirty_lo, dirty_hi) */
	uint32 dirty_hi;
	int fd;			/* the host file, -1: contents lost at exit */
} Fm25v02Type;

extern const Stm32SpiChipOps fm25v02_ops;

/* Blank contents (0x00); path: the host file to load and keep them in, or NULL. */
void fm25v02_open(Fm25v02Type *chip, const char *path);

#endif

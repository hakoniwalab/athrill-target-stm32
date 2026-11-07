/*
 * The SPI chip operations the target's SPI controllers used to call (they are
 * athrill-device models now); kept only for the chip fixtures that
 * test_v6c_sensors calls directly (#44).
 * SPDX-License-Identifier: MIT
 */
#ifndef ATHRILL_TEST_FIXTURE_STM32_SPI_H
#define ATHRILL_TEST_FIXTURE_STM32_SPI_H

#include "std_types.h"

typedef struct {
	/* the chip select went low (TRUE) or high (FALSE) */
	void (*select)(void *chip, std_bool selected);
	/* one byte out (MOSI), the chip's byte back (MISO) */
	uint8 (*exchange)(void *chip, uint8 mosi);
} Stm32SpiChipOps;

#endif

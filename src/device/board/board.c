/*
 * The boards (see board.h).
 */
#include "board.h"
#include "std_errno.h"
#include "cpuemu_ops.h"
#include "peripheral/stm32_gpio.h"
#include "peripheral/stm32_spi.h"
#include "sensor/ms5611.h"
#include <stdio.h>
#include <string.h>

static Ms5611Type baro;

/* Pixhawk 4: PX4 boards/px4/fmu-v5/src/spi.cpp, i2c.cpp. */
static void attach_fmu_v5(void)
{
	ms5611_reset(&baro);
	stm32_spi_attach(4U, STM32_GPIO_PORT('F'), 10U, &ms5611_ops, &baro);
}

void board_attach(void)
{
	char *name = NULL;

	if (cpuemu_get_devcfg_string("DEVICE_CONFIG_STM32_BOARD", &name) != STD_E_OK) {
		return;
	}
	if (strcmp(name, "fmu-v5") == 0) {
		attach_fmu_v5();
	}
	else {
		printf("WARNING: unknown board %s (DEVICE_CONFIG_STM32_BOARD)\n", name);
	}
}

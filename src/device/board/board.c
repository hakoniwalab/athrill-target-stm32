/*
 * The boards (see board.h).
 */
#include "board.h"
#include "std_errno.h"
#include "cpuemu_ops.h"
#include "peripheral/stm32_gpio.h"
#include "peripheral/stm32_spi.h"
#include "sensor/ms5611.h"
#include "sensor/icm20689.h"
#include "sensor/ist8310.h"
#include "sensor/ublox.h"
#include "memory/fm25v02.h"
#include "peripheral/stm32_i2c.h"
#include <stdio.h>
#include <string.h>

static Ms5611Type baro;
static Icm20689Type imu;
static Ist8310Type mag;
static UbloxType gps;
static Fm25v02Type fram = { .fd = -1 };
static std_bool has_imu;

/* Pixhawk 4: PX4 boards/px4/fmu-v5/src/spi.cpp, i2c.cpp. */
static void attach_fmu_v5(void)
{
	uint32 mhz = 216U;
	char *fram_file = NULL;

	(void)cpuemu_get_devcfg_value("DEVICE_CPU_FREQ", &mhz);
	icm20689_reset(&imu, (uint64)mhz * 1000000U, ICM20689_ROTATION_YAW_90, STM32_GPIO_PORT('B'), 4U);
	stm32_spi_attach(1U, STM32_GPIO_PORT('F'), 2U, &icm20689_ops, &imu);
	has_imu = TRUE;
	ublox_reset(&gps, 1U, (uint64)mhz * 1000000U);
	stm32_uart_attach(1U, &ublox_ops, &gps);
	ist8310_reset(&mag, IST8310_ROTATION_ROLL_180_YAW_90);
	stm32_i2c_attach(3U, 0x0EU, &ist8310_ops, &mag);
	ms5611_reset(&baro);
	stm32_spi_attach(4U, STM32_GPIO_PORT('F'), 10U, &ms5611_ops, &baro);
	/* the parameters: F-RAM on SPI2, kept in DEVICE_CONFIG_STM32_FRAM_FILE if given */
	if (cpuemu_get_devcfg_string("DEVICE_CONFIG_STM32_FRAM_FILE", &fram_file) != STD_E_OK) {
		fram_file = NULL;
	}
	fm25v02_open(&fram, fram_file);
	stm32_spi_attach(2U, STM32_GPIO_PORT('F'), 5U, &fm25v02_ops, &fram);
}

void board_attach(void)
{
	char *name = NULL;

	has_imu = FALSE;
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

void board_tick(DeviceClockType *clock)
{
	if (has_imu) {
		icm20689_tick(&imu, clock);
		ublox_tick(&gps, clock);
	}
}

/* Pixhawk 6C external chips, wired from the PX4 board definition. */
#include "fmu_v6c.h"
#include "cpuemu_ops.h"
#include "std_errno.h"
#include "peripheral/stm32_gpio.h"
#include "peripheral/stm32_spi.h"
#include "peripheral/stm32_i2c.h"
#include "memory/fm25v02.h"
#include "sensor/bmi055.h"
#include "sensor/icm42688p.h"
#include "sensor/ms5611.h"
#include "sensor/ist8310.h"

static Fm25v02Type fram = { .fd = -1 };
static Bmi055Type bmi055_accel;
static Bmi055Type bmi055_gyro;
static Icm42688pType icm42688p;
static Ms5611Type ms5611;
static Ist8310Type ist8310;
static std_bool attached;

void fmu_v6c_attach(void)
{
	uint32 mhz = 480U;
	char *fram_file = NULL;

	(void)cpuemu_get_devcfg_value("DEVICE_CPU_FREQ", &mhz);
	/* SPI1 PC15, DRDY PE4; PX4 boards/px4/fmu-v6c/src/spi.cpp, rotation boards/px4/fmu-v6c/init/rc.board_sensors. */
	bmi055_reset(&bmi055_accel, BMI055_ACCEL, (uint64)mhz * 1000000U,
		BMI055_ROTATION_YAW_180, STM32_GPIO_PORT('E'), 4U);
	stm32_spi_attach(1U, STM32_GPIO_PORT('C'), 15U, &bmi055_ops, &bmi055_accel);
	/* SPI1 PC14, DRDY PE5; PX4 boards/px4/fmu-v6c/src/spi.cpp, rotation boards/px4/fmu-v6c/init/rc.board_sensors. */
	bmi055_reset(&bmi055_gyro, BMI055_GYRO, (uint64)mhz * 1000000U,
		BMI055_ROTATION_YAW_180, STM32_GPIO_PORT('E'), 5U);
	stm32_spi_attach(1U, STM32_GPIO_PORT('C'), 14U, &bmi055_ops, &bmi055_gyro);
	/* SPI1 PC13, DRDY PE6; PX4 boards/px4/fmu-v6c/src/spi.cpp, rotation boards/px4/fmu-v6c/init/rc.board_sensors. */
	icm42688p_reset(&icm42688p, (uint64)mhz * 1000000U,
		ICM42688P_ROTATION_YAW_270, STM32_GPIO_PORT('E'), 6U);
	stm32_spi_attach(1U, STM32_GPIO_PORT('C'), 13U, &icm42688p_ops, &icm42688p);

	/* I2C4 0x77; PX4 boards/px4/fmu-v6c/src/i2c.cpp, rotation boards/px4/fmu-v6c/init/rc.board_sensors. */
	ms5611_reset(&ms5611);
	stm32_i2c_attach(4U, 0x77U, &ms5611_i2c_ops, &ms5611);
	/* I2C4 0x0c; PX4 boards/px4/fmu-v6c/src/i2c.cpp, rotation boards/px4/fmu-v6c/init/rc.board_sensors. */
	ist8310_reset(&ist8310, IST8310_ROTATION_NONE);
	stm32_i2c_attach(4U, 0x0CU, &ist8310_ops, &ist8310);

	/* SPI2 PD4; PX4 boards/px4/fmu-v6c/src/spi.cpp. */
	if (cpuemu_get_devcfg_string("DEVICE_CONFIG_STM32_FRAM_FILE", &fram_file) == STD_E_OK) {
		fm25v02_open(&fram, fram_file);
		stm32_spi_attach(2U, STM32_GPIO_PORT('D'), 4U, &fm25v02_ops, &fram);
	}
	attached = TRUE;
}

void fmu_v6c_tick(DeviceClockType *clock)
{
	if (!attached) {
		return;
	}
	bmi055_tick(&bmi055_accel, clock);
	bmi055_tick(&bmi055_gyro, clock);
	icm42688p_tick(&icm42688p, clock);
}

/*
 * The boards (see board.h).
 */
#include "board.h"
#include "fmu_v6c.h"
#include "std_errno.h"
#include "cpuemu_ops.h"
#include "peripheral/stm32_gpio.h"
#include "peripheral/stm32_spi.h"
#include "sensor/ms5611.h"
#include "sensor/icm20689.h"
#include "sensor/ist8310.h"
#include "sensor/ublox.h"
#include "memory/fm25v02.h"
#include "memory/sdcard.h"
#include "peripheral/stm32_sdmmc.h"
#include "peripheral/stm32_i2c.h"
#include "peripheral/stm32_tim.h"
#include "sensor/sensor_source.h"
#include <stdio.h>
#include <string.h>

static Ms5611Type baro;
static Icm20689Type imu;
static Ist8310Type mag;
static UbloxType gps;
static Fm25v02Type fram = { .fd = -1 };
static SdcardType sdcard = { .fd = -1 };
static std_bool has_imu;

/*
 * The ESCs on the FMU's PWM outputs (Pixhawk 4: AUX1 to AUX8, PX4
 * boards/px4/fmu-v5/src/timer_config.cpp) and the motor command each takes
 * from its pulse: 1000 us is 0, 2000 us is 1 (PX4's PWM_AUX_MIN/MAX
 * defaults), clamped, 0 without a pulse. The commands go to the sensor
 * device as the motor outputs of hil_actuator whenever one changes.
 */
#define MOTORS		8U
#define PULSE_MIN_US	1000.0F
#define PULSE_MAX_US	2000.0F

typedef struct {
	uint32 timer;
	uint32 channel;
} PwmOutputType;

static const PwmOutputType fmu_v5_outputs[MOTORS] = {
	{ 1U, 4U }, { 1U, 3U }, { 1U, 2U }, { 1U, 1U }, { 4U, 2U }, { 4U, 3U }, { 12U, 1U }, { 12U, 2U },
};
static const PwmOutputType *motor_outputs;
static float32 motor_commands[MOTORS];
static uint32 clocks_per_us = 216U;

static float32 motor_command(const PwmOutputType *output)
{
	Stm32TimPwmType pwm;
	float32 us;
	float32 command;

	if (!stm32_tim_pwm(output->timer, output->channel, &pwm) || !pwm.enabled || (pwm.period_clocks == 0U)) {
		return 0.0F;
	}
	us = (float32)pwm.high_clocks / (float32)clocks_per_us;
	command = (us - PULSE_MIN_US) / (PULSE_MAX_US - PULSE_MIN_US);
	if (command < 0.0F) {
		return 0.0F;
	}
	return (command > 1.0F) ? 1.0F : command;
}

static void update_motors(void)
{
	std_bool changed = FALSE;
	uint32 k;

	for (k = 0U; k < MOTORS; k++) {
		float32 command = motor_command(&motor_outputs[k]);

		if (command != motor_commands[k]) {
			motor_commands[k] = command;
			changed = TRUE;
		}
	}
	if (changed) {
		sensor_source_actuators(motor_commands, MOTORS);
	}
}

/* Pixhawk 4: PX4 boards/px4/fmu-v5/src/spi.cpp, i2c.cpp. */
static void attach_fmu_v5(void)
{
	uint32 mhz = 216U;
	char *fram_file = NULL;
	char *sd_image = NULL;

	(void)cpuemu_get_devcfg_value("DEVICE_CPU_FREQ", &mhz);
	clocks_per_us = mhz;
	motor_outputs = fmu_v5_outputs;
	memset(motor_commands, 0, sizeof(motor_commands));
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
	/* the microSD card on SDMMC1, an image in DEVICE_CONFIG_STM32_SDCARD_IMAGE (none: empty slot) */
	if (cpuemu_get_devcfg_string("DEVICE_CONFIG_STM32_SDCARD_IMAGE", &sd_image) != STD_E_OK) {
		sd_image = NULL;
	}
	sdcard_open(&sdcard, sd_image);
	if (sdcard_present(&sdcard)) {
		stm32_sdmmc_attach(&sdcard);
		stm32_gpio_drive(STM32_GPIO_PORT('C'), 8U, TRUE);	/* D0 high: the card is not busy */
	}
}

void board_attach(void)
{
	char *name = NULL;

	has_imu = FALSE;
	motor_outputs = NULL;
	if (cpuemu_get_devcfg_string("DEVICE_CONFIG_STM32_BOARD", &name) != STD_E_OK) {
		return;
	}
	if (strcmp(name, "fmu-v5") == 0) {
		attach_fmu_v5();
	}
	else if (strcmp(name, "fmu-v6c") == 0) {
		fmu_v6c_attach();
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
	if (motor_outputs != NULL) {
		update_motors();
	}
}

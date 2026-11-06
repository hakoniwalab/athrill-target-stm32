/*
 * The board around the STM32: which chips sit on which buses and pins
 * (DEVICE_CONFIG_STM32_BOARD; none by default).
 *   fmu-v5  Pixhawk 4 (PX4 boards/px4/fmu-v5): ICM-20689 on SPI1 (CS PF2,
 *           data ready PB4, mounted yaw 90), MS5611 on SPI4 (CS PF10),
 *           IST8310 on I2C3 (0x0E, mounted roll 180 yaw 90), a u-blox M8
 *           GPS on USART1 (GPS1, /dev/ttyS0), the parameters' F-RAM on SPI2,
 *           the microSD card on SDMMC1, and ESCs on the PWM outputs AUX1
 *           to AUX8 whose motor commands go to the sensor device
 *           (hil_actuator)
 *   fmu-v6c Pixhawk 6C, STM32H743 profile: BMI055 accel and gyro plus an
 *           ICM-42688-P on SPI1, MS5611 and IST8310 on I2C4, and F-RAM
 *           on SPI2.
 */
#ifndef ATHRILL_STM32_BOARD_H
#define ATHRILL_STM32_BOARD_H

#include "std_types.h"
#include "cpu.h"
#include "std_device_ops.h"

typedef struct {
	uint32 timer;
	uint32 channel;
} BoardPwmOutputType;

/* Selects the board's ordered PWM outputs for the shared actuator path. */
void board_pwm_outputs(const BoardPwmOutputType *outputs, uint32 count, uint32 cpu_mhz);
/* Puts the chips of the configured board on their buses (after the buses' reset). */
void board_attach(void);
/* The board's chips up to the device clock (their pins, their next events). */
void board_tick(DeviceClockType *clock);

#endif

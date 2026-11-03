/*
 * The board around the STM32: which chips sit on which buses and pins
 * (DEVICE_CONFIG_STM32_BOARD; none by default).
 *   fmu-v5  Pixhawk 4 (PX4 boards/px4/fmu-v5): ICM-20689 on SPI1 (CS PF2,
 *           data ready PB4, mounted yaw 90), MS5611 on SPI4 (CS PF10),
 *           IST8310 on I2C3 (0x0E, mounted roll 180 yaw 90), a u-blox M8
 *           GPS on USART1 (GPS1, /dev/ttyS0)
 */
#ifndef ATHRILL_STM32_BOARD_H
#define ATHRILL_STM32_BOARD_H

#include "std_types.h"
#include "cpu.h"
#include "std_device_ops.h"

/* Puts the chips of the configured board on their buses (after the buses' reset). */
void board_attach(void);
/* The board's chips up to the device clock (their pins, their next events). */
void board_tick(DeviceClockType *clock);

#endif

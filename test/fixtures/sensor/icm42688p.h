/*
 * TDK InvenSense ICM-42688-P SPI model: banked register file, reset and
 * power state, 16/20-byte FIFO packets, FIFO count modes and INT1 threshold
 * pulses.  It accepts the configurations used by both PX4 and ArduPilot.
 */
#ifndef ATHRILL_STM32_ICM42688P_H
#define ATHRILL_STM32_ICM42688P_H

#include "std_types.h"
#include "cpu.h"
#include "std_device_ops.h"
#include "peripheral/stm32_spi.h"

#define ICM42688P_FIFO_BYTES 2048U

typedef enum {
	ICM42688P_ROTATION_NONE = 0,
	ICM42688P_ROTATION_YAW_270 = 6,
} Icm42688pRotationType;

typedef struct {
	uint8 regs[5][128];
	Icm42688pRotationType rotation;
	uint32 drdy_port;
	uint32 drdy_pin;
	uint64 clocks_per_sample;
	uint8 bank;
	std_bool started;
	std_bool reading;
	uint8 address;
	uint64 fifo_start;
	uint64 fifo_read_bytes;
	uint8 record[20];
	uint64 next_drdy;
} Icm42688pType;

extern const Stm32SpiChipOps icm42688p_ops;

void icm42688p_reset(Icm42688pType *chip, uint64 cpu_hz,
	Icm42688pRotationType rotation, uint32 drdy_port, uint32 drdy_pin);
void icm42688p_tick(Icm42688pType *chip, DeviceClockType *clock);

#endif /* ATHRILL_STM32_ICM42688P_H */

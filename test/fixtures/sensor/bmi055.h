/*
 * Bosch BMI055 accelerometer and gyroscope SPI models.  The two dies have
 * separate chip selects and register files but share the FIFO mechanics.
 * Samples come from sensor_source, are converted to the Bosch sensor axes,
 * and fill the six-byte little-endian FIFOs at 2 kHz.
 */
#ifndef ATHRILL_STM32_BMI055_H
#define ATHRILL_STM32_BMI055_H

#include "std_types.h"
#include "cpu.h"
#include "std_device_ops.h"
#include "peripheral/stm32_spi.h"

typedef enum {
	BMI055_ACCEL = 0,
	BMI055_GYRO = 1,
} Bmi055KindType;

typedef enum {
	BMI055_ROTATION_NONE = 0,
	BMI055_ROTATION_YAW_180 = 4,
} Bmi055RotationType;

typedef struct {
	uint8 regs[64];
	Bmi055KindType kind;
	Bmi055RotationType rotation;
	uint32 drdy_port;
	uint32 drdy_pin;
	uint64 clocks_per_sample;
	std_bool started;
	std_bool reading;
	uint8 address;
	uint64 fifo_start;
	uint64 fifo_read_frames;
	uint8 record[6];
	uint64 next_drdy;
} Bmi055Type;

extern const Stm32SpiChipOps bmi055_ops;

void bmi055_reset(Bmi055Type *chip, Bmi055KindType kind, uint64 cpu_hz,
	Bmi055RotationType rotation, uint32 drdy_port, uint32 drdy_pin);
void bmi055_tick(Bmi055Type *chip, DeviceClockType *clock);

#endif /* ATHRILL_STM32_BMI055_H */

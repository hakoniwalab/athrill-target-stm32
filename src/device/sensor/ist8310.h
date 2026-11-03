/*
 * The iSentek IST8310 3-axis magnetometer on I2C (address 0x0E): WAI 0x10,
 * single measurement (CNTL1), soft reset (CNTL2), the control registers
 * read back as written, STAT1.DRDY and the data registers. A measurement
 * takes the sensor source's field at the time it is commanded, in the
 * chip's axes (x forward, y right, z up, and the board's mounting) at
 * 1320 LSB/gauss. Lightweight: the result is ready at once; no
 * temperature, no self-test, no cross-axis data (reads 0).
 */
#ifndef ATHRILL_STM32_IST8310_H
#define ATHRILL_STM32_IST8310_H

#include "std_types.h"
#include "peripheral/stm32_i2c.h"

/* The chip's mounting on the board, as PX4's rotation of its samples (-R). */
typedef enum {
	IST8310_ROTATION_NONE = 0,
	IST8310_ROTATION_ROLL_180_YAW_90 = 10,
} Ist8310RotationType;

typedef struct {
	uint8 regs[256];
	Ist8310RotationType rotation;
	uint8 pointer;		/* the register address */
	std_bool address_next;	/* the next byte written is the register address */
} Ist8310Type;

extern const Stm32I2cChipOps ist8310_ops;

void ist8310_reset(Ist8310Type *chip, Ist8310RotationType rotation);

#endif

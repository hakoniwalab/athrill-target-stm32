/*
 * The MS5611-01BA03 barometric pressure sensor on SPI (TE Connectivity
 * datasheet): reset, the PROM with its calibration words and CRC, D1/D2
 * conversions and the ADC read. A conversion takes the sensor source's
 * pressure and temperature at the time it is commanded, as raw D1/D2 values
 * that give them back through the datasheet's compensation (first and
 * second order); a D1 conversion fits the last D2, which the driver
 * compensates it with. Each conversion adds the ADC's noise of the datasheet for
 * its oversampling ratio (RMS 0.065 to 0.012 mbar, 0.012 to 0.002 degrees C
 * for OSR 256 to 4096) from a fixed-seed generator, so runs repeat; like the
 * real chip, its readings are never exactly constant (PX4 takes a reading
 * that does not change as a stale sensor). Lightweight: the result is
 * ready at once.
 */
#ifndef ATHRILL_STM32_MS5611_H
#define ATHRILL_STM32_MS5611_H

#include "std_types.h"
#include "peripheral/stm32_spi.h"

/* The I2C chip operations the target's I2C controller used to call (it is an
 * athrill-device model now); kept for test_v6c_sensors, which calls them. */
typedef struct {
	std_bool (*start)(void *chip, std_bool read);
	void (*write)(void *chip, uint8 data);
	uint8 (*read)(void *chip);
	void (*stop)(void *chip);
} Stm32I2cChipOps;

typedef struct {
	uint16 prom[8];
	uint8 command;		/* the first byte after chip select; 0xFF: none yet */
	uint8 out[3];		/* the bytes the command answers */
	uint32 out_len;
	uint32 out_pos;
	uint32 adc;		/* the last conversion */
	uint32 d1;		/* raw pressure, raw temperature for the source's sample */
	uint32 d2;
	std_bool has_d2;	/* a D2 conversion was made (D1 then fits it) */
	std_bool noise;		/* the ADC noise (TRUE after reset) */
	uint64 noise_state;	/* its generator */
} Ms5611Type;

extern const Stm32SpiChipOps ms5611_ops;
/* The same command engine behind the chip's I2C interface. */
extern const Stm32I2cChipOps ms5611_i2c_ops;

void ms5611_reset(Ms5611Type *chip);
/* The raw D1 (pressure) and D2 (temperature) of pressure (hPa) and temperature (degrees C). */
void ms5611_raw(const Ms5611Type *chip, float32 pressure, float32 temperature, uint32 *d1, uint32 *d2);
/* The datasheet's compensation: pressure (0.01 hPa) and temperature (0.01 degrees C) of D1, D2. */
void ms5611_compensate(const Ms5611Type *chip, uint32 d1, uint32 d2, sint32 *pressure, sint32 *temperature);

#endif

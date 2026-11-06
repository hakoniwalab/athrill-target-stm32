/*
 * The MS5611 on SPI (see ms5611.h). Commands and the compensation from the
 * MS5611-01BA03 datasheet; the PROM CRC from application note AN520.
 */
#include "ms5611.h"
#include <math.h>
#include "sensor_source.h"

#define CMD_RESET	0x1EU
#define CMD_CONVERT_D1	0x40U	/* 0x40 to 0x48: OSR 256 to 4096 */
#define CMD_CONVERT_D2	0x50U
#define CMD_ADC_READ	0x00U
#define CMD_PROM_READ	0xA0U	/* 0xA0 to 0xAE: words 0 to 7 */
#define RAW_MAX		0xFFFFFFU

/* Calibration words C1 to C6: the datasheet's typical values. */
static const uint16 calibration[6] = { 40127U, 36924U, 23317U, 23282U, 33464U, 28312U };

/* AN520: the CRC-4 of the PROM (word 7 bits 3:0), computed with those bits zero. */
static uint16 prom_crc4(const uint16 *prom)
{
	uint16 rem = 0U;
	uint32 cnt;
	uint32 bit;

	for (cnt = 0U; cnt < 16U; cnt++) {
		uint16 w = (cnt == 14U || cnt == 15U) ? (uint16)(prom[7] & 0xFF00U) : prom[cnt >> 1];

		if ((cnt & 1U) != 0U) {
			rem ^= (uint16)(w & 0x00FFU);
		}
		else {
			rem ^= (uint16)(w >> 8);
		}
		for (bit = 8U; bit > 0U; bit--) {
			rem = ((rem & 0x8000U) != 0U) ? (uint16)((rem << 1) ^ 0x3000U) : (uint16)(rem << 1);
		}
	}
	return (uint16)((rem >> 12) & 0xFU);
}

void ms5611_reset(Ms5611Type *chip)
{
	uint32 i;

	chip->prom[0] = 0U;	/* factory data */
	for (i = 0U; i < 6U; i++) {
		chip->prom[i + 1U] = calibration[i];
	}
	chip->prom[7] = 0U;
	chip->prom[7] |= prom_crc4(chip->prom);
	chip->command = 0xFFU;
	chip->out_len = 0U;
	chip->out_pos = 0U;
	chip->adc = 0U;
	chip->noise = TRUE;
	chip->has_d2 = FALSE;
	chip->noise_state = 0x9E3779B97F4A7C15ULL;
}

/* The RMS noise of OSR 256, 512, 1024, 2048, 4096 (datasheet: resolution RMS). */
static const float32 pressure_noise_mbar[5] = { 0.065F, 0.042F, 0.027F, 0.018F, 0.012F };
static const float32 temperature_noise_c[5] = { 0.012F, 0.008F, 0.005F, 0.003F, 0.002F };

/* A standard normal value (Box-Muller on xorshift64). */
static float32 gaussian(Ms5611Type *chip)
{
	float64 u[2];
	uint32 k;

	for (k = 0U; k < 2U; k++) {
		chip->noise_state ^= chip->noise_state << 13;
		chip->noise_state ^= chip->noise_state >> 7;
		chip->noise_state ^= chip->noise_state << 17;
		u[k] = ((float64)(chip->noise_state >> 11) + 0.5) / 9007199254740992.0;
	}
	return (float32)(sqrt(-2.0 * log(u[0])) * cos(6.283185307179586 * u[1]));
}

void ms5611_compensate(const Ms5611Type *chip, uint32 d1, uint32 d2, sint32 *pressure, sint32 *temperature)
{
	sint64 dt = (sint64)d2 - ((sint64)chip->prom[5] << 8);
	sint64 temp = 2000 + ((dt * chip->prom[6]) >> 23);
	sint64 off = ((sint64)chip->prom[2] << 16) + (((sint64)chip->prom[4] * dt) >> 7);
	sint64 sens = ((sint64)chip->prom[1] << 15) + (((sint64)chip->prom[3] * dt) >> 8);

	if (temp < 2000) {
		sint64 t2 = (dt * dt) >> 31;
		sint64 f = (temp - 2000) * (temp - 2000);
		sint64 off2 = (5 * f) >> 1;
		sint64 sens2 = (5 * f) >> 2;

		if (temp < -1500) {
			sint64 f2 = (temp + 1500) * (temp + 1500);

			off2 += 7 * f2;
			sens2 += (11 * f2) >> 1;
		}
		temp -= t2;
		off -= off2;
		sens -= sens2;
	}
	*temperature = (sint32)temp;
	*pressure = (sint32)(((((sint64)d1 * sens) >> 21) - off) >> 15);
}

/* Both results rise with the raw values: the smallest raw value that reaches want. */
static uint32 search(const Ms5611Type *chip, std_bool for_pressure, uint32 fixed, sint32 want)
{
	uint32 lo = 0U;
	uint32 hi = RAW_MAX;

	while (lo < hi) {
		uint32 mid = lo + ((hi - lo) / 2U);
		sint32 p;
		sint32 t;

		if (for_pressure) {
			ms5611_compensate(chip, mid, fixed, &p, &t);
		}
		else {
			ms5611_compensate(chip, 0U, mid, &p, &t);
		}
		if ((for_pressure ? p : t) < want) {
			lo = mid + 1U;
		}
		else {
			hi = mid;
		}
	}
	return lo;
}

/* value * 100 to the nearest integer (0.01 hPa, 0.01 degrees C). */
static sint32 hundredths(float32 value)
{
	return (sint32)lround((float64)value * 100.0);
}

void ms5611_raw(const Ms5611Type *chip, float32 pressure, float32 temperature, uint32 *d1, uint32 *d2)
{
	*d2 = search(chip, FALSE, 0U, hundredths(temperature));
	*d1 = search(chip, TRUE, *d2, hundredths(pressure));
}

static void chip_select(void *p, std_bool selected)
{
	Ms5611Type *chip = (Ms5611Type *)p;

	chip->command = 0xFFU;
	chip->out_len = 0U;
	chip->out_pos = 0U;
	(void)selected;
}

static void command(Ms5611Type *chip, uint8 cmd)
{
	SensorSampleType sample;

	chip->command = cmd;
	if (cmd == CMD_RESET) {
		chip->adc = 0U;
	}
	else if ((cmd & 0xF0U) == CMD_CONVERT_D1 || (cmd & 0xF0U) == CMD_CONVERT_D2) {
		uint32 osr = ((uint32)cmd >> 1) & 7U;

		std_bool d1 = ((cmd & 0xF0U) == CMD_CONVERT_D1);

		sensor_source_sample(&sample);
		if (chip->noise && (osr < 5U)) {
			/* each conversion has its own noise: D1 the pressure's, D2 the temperature's */
			if (d1) {
				sample.abs_pressure += pressure_noise_mbar[osr] * gaussian(chip);
			}
			else {
				sample.temperature += temperature_noise_c[osr] * gaussian(chip);
			}
		}
		if (d1 && chip->has_d2) {
			/* D1 that the driver's compensation with the D2 it read gives the pressure */
			chip->d1 = search(chip, TRUE, chip->d2, hundredths(sample.abs_pressure));
		}
		else {
			ms5611_raw(chip, sample.abs_pressure, sample.temperature, &chip->d1, &chip->d2);
			chip->has_d2 = chip->has_d2 || !d1;
		}
		chip->adc = d1 ? chip->d1 : chip->d2;
	}
	else if (cmd == CMD_ADC_READ) {
		chip->out[0] = (uint8)(chip->adc >> 16);
		chip->out[1] = (uint8)(chip->adc >> 8);
		chip->out[2] = (uint8)chip->adc;
		chip->out_len = 3U;
	}
	else if ((cmd & 0xF0U) == CMD_PROM_READ) {
		uint16 w = chip->prom[(cmd >> 1) & 7U];

		chip->out[0] = (uint8)(w >> 8);
		chip->out[1] = (uint8)w;
		chip->out_len = 2U;
	}
}

static uint8 chip_exchange(void *p, uint8 mosi)
{
	Ms5611Type *chip = (Ms5611Type *)p;

	if (chip->command == 0xFFU) {
		command(chip, mosi);
		return 0U;
	}
	if (chip->out_pos < chip->out_len) {
		return chip->out[chip->out_pos++];
	}
	return 0U;
}

const Stm32SpiChipOps ms5611_ops = { chip_select, chip_exchange };

static std_bool i2c_start(void *p, std_bool read)
{
	Ms5611Type *chip = (Ms5611Type *)p;

	if (read) {
		chip->out_pos = 0U;
	}
	else {
		chip->command = 0xFFU;
		chip->out_len = 0U;
		chip->out_pos = 0U;
	}
	return TRUE;
}

static void i2c_write(void *p, uint8 data)
{
	Ms5611Type *chip = (Ms5611Type *)p;

	if (chip->command == 0xFFU) {
		command(chip, data);
	}
}

static uint8 i2c_read(void *p)
{
	Ms5611Type *chip = (Ms5611Type *)p;

	return (chip->out_pos < chip->out_len) ? chip->out[chip->out_pos++] : 0U;
}

static void i2c_stop(void *p)
{
	(void)p;
}

const Stm32I2cChipOps ms5611_i2c_ops = { i2c_start, i2c_write, i2c_read, i2c_stop };

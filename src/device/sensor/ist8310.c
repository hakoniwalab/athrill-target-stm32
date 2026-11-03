/*
 * The IST8310 on I2C (see ist8310.h). Register addresses from the
 * datasheet's register map, as PX4's driver uses them.
 */
#include "ist8310.h"
#include "sensor_source.h"

#define REG_WAI		0x00U
#define REG_STAT1	0x02U
#define REG_DATAXL	0x03U	/* to DATAZH 0x08 */
#define REG_CNTL1	0x0AU
#define REG_CNTL2	0x0BU

#define WAI_VALUE	0x10U
#define STAT1_DRDY	(1U << 0)
#define CNTL1_SINGLE	0x01U
#define CNTL2_SRST	(1U << 0)
#define LSB_PER_GAUSS	1320.0F

static sint16 saturate(float32 v)
{
	if (v >= 32767.0F) {
		return 32767;
	}
	if (v <= -32768.0F) {
		return -32768;
	}
	return (sint16)((v < 0.0F) ? (v - 0.5F) : (v + 0.5F));
}

static void reset_registers(Ist8310Type *chip)
{
	uint32 i;

	for (i = 0U; i < 256U; i++) {
		chip->regs[i] = 0U;
	}
	chip->regs[REG_WAI] = WAI_VALUE;
}

/*
 * A single measurement. PX4 reads the chip as (x, y, -z) and then applies
 * the board's rotation; this undoes both from body front-right-down.
 */
static void measure(Ist8310Type *chip)
{
	SensorSampleType s;
	float32 raw[3];
	uint32 k;

	sensor_source_sample(&s);
	if (chip->rotation == IST8310_ROTATION_ROLL_180_YAW_90) {
		/* roll 180, yaw 90: body = (v_y, v_x, -v_z), v = (x, y, -z) */
		raw[0] = s.mag[1];
		raw[1] = s.mag[0];
		raw[2] = s.mag[2];
	}
	else {
		raw[0] = s.mag[0];
		raw[1] = s.mag[1];
		raw[2] = -s.mag[2];
	}
	for (k = 0U; k < 3U; k++) {
		sint16 v = saturate(raw[k] * LSB_PER_GAUSS);

		chip->regs[REG_DATAXL + (2U * k)] = (uint8)v;
		chip->regs[REG_DATAXL + (2U * k) + 1U] = (uint8)((uint16)v >> 8);
	}
	chip->regs[REG_STAT1] |= STAT1_DRDY;
	chip->regs[REG_CNTL1] = 0U;	/* back to stand-by */
}

static std_bool chip_start(void *p, std_bool read)
{
	Ist8310Type *chip = (Ist8310Type *)p;

	chip->address_next = !read;
	return TRUE;
}

static void chip_write(void *p, uint8 data)
{
	Ist8310Type *chip = (Ist8310Type *)p;
	uint8 r;

	if (chip->address_next) {
		chip->pointer = data;
		chip->address_next = FALSE;
		return;
	}
	r = chip->pointer++;
	switch (r) {
	case REG_CNTL1:
		chip->regs[r] = data;
		if ((data & 0x0FU) == CNTL1_SINGLE) {
			measure(chip);
		}
		break;
	case REG_CNTL2:
		if ((data & CNTL2_SRST) != 0U) {
			reset_registers(chip);	/* SRST reads back 0 once done */
		}
		else {
			chip->regs[r] = data;
		}
		break;
	case REG_WAI:
	case REG_STAT1:
		break;
	default:
		chip->regs[r] = data;
		break;
	}
}

static uint8 chip_read(void *p)
{
	Ist8310Type *chip = (Ist8310Type *)p;
	uint8 r = chip->pointer++;
	uint8 v = chip->regs[r];

	/* reading the data clears data ready */
	if ((r >= REG_DATAXL) && (r <= (REG_DATAXL + 5U))) {
		chip->regs[REG_STAT1] &= (uint8)~STAT1_DRDY;
	}
	return v;
}

static void chip_stop(void *p)
{
	(void)p;
}

const Stm32I2cChipOps ist8310_ops = { chip_start, chip_write, chip_read, chip_stop };

void ist8310_reset(Ist8310Type *chip, Ist8310RotationType rotation)
{
	reset_registers(chip);
	chip->rotation = rotation;
	chip->pointer = 0U;
	chip->address_next = FALSE;
}

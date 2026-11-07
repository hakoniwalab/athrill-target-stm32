/* ICM-42688-P register, FIFO and interrupt behavior (see icm42688p.h). */
#include "icm42688p.h"
#include "sensor_source.h"
#include "cpuemu_ops.h"
#include "peripheral/stm32_gpio.h"

#define REG_DEVICE_CONFIG	0x11U
#define REG_INT_CONFIG		0x14U
#define REG_FIFO_CONFIG		0x16U
#define REG_TEMP_DATA1		0x1DU
#define REG_ACCEL_DATA_X1	0x1FU
#define REG_GYRO_DATA_X1	0x25U
#define REG_INT_STATUS		0x2DU
#define REG_FIFO_COUNTH		0x2EU
#define REG_FIFO_COUNTL		0x2FU
#define REG_FIFO_DATA		0x30U
#define REG_SIGNAL_PATH_RESET	0x4BU
#define REG_INTF_CONFIG0	0x4CU
#define REG_PWR_MGMT0		0x4EU
#define REG_GYRO_CONFIG0	0x4FU
#define REG_ACCEL_CONFIG0	0x50U
#define REG_FIFO_CONFIG1	0x5FU
#define REG_FIFO_CONFIG2	0x60U
#define REG_FIFO_CONFIG3	0x61U
#define REG_INT_SOURCE0		0x65U
#define REG_WHO_AM_I		0x75U
#define REG_BANK_SEL		0x76U

#define WHO_AM_I_VALUE		0x47U
#define DEVICE_SOFT_RESET	0x01U
#define RESET_DONE_INT		0x10U
#define FIFO_THS_INT		0x04U
#define FIFO_FLUSH		0x02U
#define FIFO_COUNT_REC		0x40U
#define FIFO_COUNT_ENDIAN	0x20U
#define SENSOR_DATA_ENDIAN	0x10U
#define FIFO_HIRES_EN		0x10U
#define FIFO_GYRO_EN		0x02U
#define FIFO_ACCEL_EN		0x01U
#define FIFO_THS_INT1_EN	0x04U
#define GRAVITY			9.80665F
#define RAD_TO_DEG		57.29577951F

typedef struct {
	sint32 acc[3];
	sint32 gyro[3];
	sint16 temp;
} Raw20Type;

static sint32 saturate(float32 v, sint32 minimum, sint32 maximum)
{
	if (v >= (float32)maximum) {
		return maximum;
	}
	if (v <= (float32)minimum) {
		return minimum;
	}
	return (sint32)((v < 0.0F) ? (v - 0.5F) : (v + 0.5F));
}

/* Firmware flips sensor y/z; undo that and the board's mounting rotation. */
static void to_chip_axes(const Icm42688pType *chip, const float32 *frd, float32 *out)
{
	float32 v[3];

	if (chip->rotation == ICM42688P_ROTATION_YAW_270) {
		/* yaw 270: body = (v_y, -v_x, v_z) */
		v[0] = -frd[1];
		v[1] = frd[0];
		v[2] = frd[2];
	}
	else {
		v[0] = frd[0];
		v[1] = frd[1];
		v[2] = frd[2];
	}
	out[0] = v[0];
	out[1] = -v[1];
	out[2] = -v[2];
}

static void raw_sample(const Icm42688pType *chip, std_bool hires, Raw20Type *raw)
{
	SensorSampleType sample;
	float32 acc[3];
	float32 gyro[3];
	float32 acc_scale = (hires ? 8192.0F : 2048.0F) / GRAVITY;
	float32 gyro_scale = (hires ? 131.0F : 16.4F) * RAD_TO_DEG;
	uint32 k;

	sensor_source_sample(&sample);
	to_chip_axes(chip, sample.acc, acc);
	to_chip_axes(chip, sample.gyro, gyro);
	for (k = 0U; k < 3U; k++) {
		raw->acc[k] = saturate(acc[k] * acc_scale, hires ? -131072 : -32768, hires ? 131071 : 32767);
		raw->gyro[k] = saturate(gyro[k] * gyro_scale, hires ? -262144 : -32768, hires ? 262143 : 32767);
	}
	raw->temp = (sint16)saturate((sample.temperature - 25.0F) * (hires ? 132.48F : 2.0F), -32768, 32767);
}

static std_bool sampling(const Icm42688pType *chip)
{
	return (chip->regs[0][REG_PWR_MGMT0] & 0x0FU) != 0U;
}

static std_bool fifo_on(const Icm42688pType *chip)
{
	return sampling(chip)
		&& ((chip->regs[0][REG_FIFO_CONFIG1] & (FIFO_ACCEL_EN | FIFO_GYRO_EN))
			== (FIFO_ACCEL_EN | FIFO_GYRO_EN));
}

static uint32 record_bytes(const Icm42688pType *chip)
{
	return (chip->regs[0][REG_FIFO_CONFIG1] & FIFO_HIRES_EN) != 0U ? 20U : 16U;
}

static void fifo_restart(Icm42688pType *chip)
{
	chip->fifo_start = cpuemu_get_clock();
	chip->fifo_read_bytes = 0U;
	chip->next_drdy = 0U;
}

static uint32 fifo_bytes(const Icm42688pType *chip)
{
	uint64 made;
	uint64 available;

	if (!fifo_on(chip)) {
		return 0U;
	}
	made = ((cpuemu_get_clock() - chip->fifo_start) / chip->clocks_per_sample) * record_bytes(chip);
	available = (made > chip->fifo_read_bytes) ? (made - chip->fifo_read_bytes) : 0U;
	return (available > ICM42688P_FIFO_BYTES) ? ICM42688P_FIFO_BYTES : (uint32)available;
}

static uint16 fifo_count(const Icm42688pType *chip)
{
	uint32 bytes = fifo_bytes(chip);

	return (chip->regs[0][REG_INTF_CONFIG0] & FIFO_COUNT_REC) != 0U
		? (uint16)(bytes / record_bytes(chip)) : (uint16)bytes;
}

static void put16(uint8 *out, uint32 offset, sint32 value, std_bool big_endian)
{
	uint16 v = (uint16)value;

	out[offset + (big_endian ? 0U : 1U)] = (uint8)(v >> 8);
	out[offset + (big_endian ? 1U : 0U)] = (uint8)v;
}

static void make_record(Icm42688pType *chip)
{
	std_bool hires = (chip->regs[0][REG_FIFO_CONFIG1] & FIFO_HIRES_EN) != 0U;
	std_bool big_endian = (chip->regs[0][REG_INTF_CONFIG0] & SENSOR_DATA_ENDIAN) != 0U;
	Raw20Type raw;
	uint32 k;

	raw_sample(chip, hires, &raw);
	if (hires) {
		chip->record[0] = 0x78U; /* accel + gyro + 20-bit + ODR timestamp */
		for (k = 0U; k < 3U; k++) {
			uint32 acc = (uint32)(raw.acc[k] * 4) & 0xFFFFFU;
			uint32 gyro = (uint32)(raw.gyro[k] * 2) & 0xFFFFFU;

			chip->record[1U + (2U * k)] = (uint8)(acc >> 12);
			chip->record[2U + (2U * k)] = (uint8)(acc >> 4);
			chip->record[7U + (2U * k)] = (uint8)(gyro >> 12);
			chip->record[8U + (2U * k)] = (uint8)(gyro >> 4);
			/* PX4 consumes the low nibble for both paths; gyro is the denser signal. */
			chip->record[17U + k] = (uint8)((acc & 0x0FU) << 4) | (uint8)(gyro & 0x0FU);
		}
		chip->record[13] = (uint8)((uint16)raw.temp >> 8);
		chip->record[14] = (uint8)raw.temp;
		chip->record[15] = 0U;
		chip->record[16] = 7U; /* 7 * 17.0667 us: approximately one 8 kHz period */
	}
	else {
		chip->record[0] = 0x68U; /* accel + gyro + ODR timestamp */
		for (k = 0U; k < 3U; k++) {
			put16(chip->record, 1U + (2U * k), raw.acc[k], big_endian);
			put16(chip->record, 7U + (2U * k), raw.gyro[k], big_endian);
		}
		chip->record[13] = (uint8)raw.temp;
		put16(chip->record, 14U, 125, big_endian);
	}
}

static uint8 fifo_byte(Icm42688pType *chip)
{
	uint32 size = record_bytes(chip);
	uint32 pos;

	if (fifo_bytes(chip) == 0U) {
		return 0U;
	}
	pos = (uint32)(chip->fifo_read_bytes % size);
	if (pos == 0U) {
		make_record(chip);
	}
	chip->fifo_read_bytes++;
	return chip->record[pos];
}

static uint8 direct_data(Icm42688pType *chip, uint8 address)
{
	Raw20Type raw;
	std_bool big_endian = (chip->regs[0][REG_INTF_CONFIG0] & SENSOR_DATA_ENDIAN) != 0U;
	sint32 value;
	uint32 offset;

	raw_sample(chip, FALSE, &raw);
	if (address < REG_ACCEL_DATA_X1) {
		value = raw.temp;
		offset = address - REG_TEMP_DATA1;
	}
	else if (address < REG_GYRO_DATA_X1) {
		offset = address - REG_ACCEL_DATA_X1;
		value = raw.acc[offset / 2U];
	}
	else {
		offset = address - REG_GYRO_DATA_X1;
		value = raw.gyro[offset / 2U];
	}
	return (((offset & 1U) == 0U) == big_endian) ? (uint8)((uint16)value >> 8) : (uint8)value;
}

static uint8 read_register(Icm42688pType *chip, uint8 address)
{
	uint16 count;
	std_bool count_big;

	if (address == REG_BANK_SEL) {
		return chip->bank;
	}
	if (chip->bank != 0U) {
		return chip->regs[chip->bank][address];
	}
	if (address == REG_WHO_AM_I) {
		return WHO_AM_I_VALUE;
	}
	if ((address >= REG_TEMP_DATA1) && (address <= (REG_GYRO_DATA_X1 + 5U))) {
		return direct_data(chip, address);
	}
	if ((address == REG_FIFO_COUNTH) || (address == REG_FIFO_COUNTL)) {
		count = fifo_count(chip);
		count_big = (chip->regs[0][REG_INTF_CONFIG0] & FIFO_COUNT_ENDIAN) != 0U;
		return ((address == REG_FIFO_COUNTH) == count_big) ? (uint8)(count >> 8) : (uint8)count;
	}
	if (address == REG_FIFO_DATA) {
		return fifo_byte(chip);
	}
	if (address == REG_INT_STATUS) {
		uint8 status = chip->regs[0][REG_INT_STATUS];
		uint32 threshold = chip->regs[0][REG_FIFO_CONFIG2]
			| ((uint32)(chip->regs[0][REG_FIFO_CONFIG3] & 0x0FU) << 8);

		if ((threshold != 0U) && (fifo_bytes(chip) >= threshold)) {
			status |= FIFO_THS_INT;
		}
		return status;
	}
	return chip->regs[0][address];
}

static void reset_registers(Icm42688pType *chip)
{
	uint32 bank;
	uint32 reg;

	for (bank = 0U; bank < 5U; bank++) {
		for (reg = 0U; reg < 128U; reg++) {
			chip->regs[bank][reg] = 0U;
		}
	}
	chip->bank = 0U;
	chip->regs[0][REG_WHO_AM_I] = WHO_AM_I_VALUE;
	chip->regs[0][REG_INT_STATUS] = RESET_DONE_INT;
}

static void write_register(Icm42688pType *chip, uint8 address, uint8 value)
{
	std_bool was_on = fifo_on(chip);

	if (address == REG_BANK_SEL) {
		chip->bank = value & 7U;
		if (chip->bank > 4U) {
			chip->bank = 0U;
		}
		return;
	}
	if (chip->bank != 0U) {
		chip->regs[chip->bank][address] = value;
		return;
	}
	if (address == REG_DEVICE_CONFIG && (value & DEVICE_SOFT_RESET) != 0U) {
		reset_registers(chip);
		fifo_restart(chip);
		return;
	}
	if (address == REG_SIGNAL_PATH_RESET) {
		if ((value & FIFO_FLUSH) != 0U) {
			fifo_restart(chip);
		}
		chip->regs[0][address] = 0U;
		return;
	}
	if ((address == REG_WHO_AM_I) || (address == REG_INT_STATUS)
		|| (address == REG_FIFO_COUNTH) || (address == REG_FIFO_COUNTL)
		|| (address == REG_FIFO_DATA)) {
		return;
	}
	chip->regs[0][address] = value;
	if (!was_on && fifo_on(chip)) {
		fifo_restart(chip);
	}
}

static void chip_select(void *p, std_bool selected)
{
	Icm42688pType *chip = (Icm42688pType *)p;

	(void)selected;
	chip->started = FALSE;
}

static uint8 chip_exchange(void *p, uint8 mosi)
{
	Icm42688pType *chip = (Icm42688pType *)p;
	uint8 miso = 0U;

	if (!chip->started) {
		chip->started = TRUE;
		chip->reading = (mosi & 0x80U) != 0U;
		chip->address = mosi & 0x7FU;
		return 0U;
	}
	if (chip->reading) {
		miso = read_register(chip, chip->address);
	}
	else {
		write_register(chip, chip->address, mosi);
	}
	if (chip->address != REG_FIFO_DATA) {
		chip->address = (chip->address + 1U) & 0x7FU;
	}
	return miso;
}

const Stm32SpiChipOps icm42688p_ops = { chip_select, chip_exchange };

void icm42688p_reset(Icm42688pType *chip, uint64 cpu_hz,
	Icm42688pRotationType rotation, uint32 drdy_port, uint32 drdy_pin)
{
	chip->rotation = rotation;
	chip->drdy_port = drdy_port;
	chip->drdy_pin = drdy_pin;
	chip->clocks_per_sample = cpu_hz / 8000U;
	chip->started = FALSE;
	chip->reading = FALSE;
	chip->address = 0U;
	reset_registers(chip);
	fifo_restart(chip);
	stm32_gpio_drive(drdy_port, drdy_pin, TRUE);
}

void icm42688p_tick(Icm42688pType *chip, DeviceClockType *clock)
{
	uint32 threshold = chip->regs[0][REG_FIFO_CONFIG2]
		| ((uint32)(chip->regs[0][REG_FIFO_CONFIG3] & 0x0FU) << 8);
	uint32 samples;
	uint64 period;
	std_bool active_low = (chip->regs[0][REG_INT_CONFIG] & 1U) == 0U;

	stm32_gpio_drive(chip->drdy_port, chip->drdy_pin, active_low);
	if (!fifo_on(chip) || ((chip->regs[0][REG_INT_SOURCE0] & FIFO_THS_INT1_EN) == 0U)) {
		return;
	}
	if (threshold == 0U) {
		threshold = record_bytes(chip);
	}
	samples = (threshold + record_bytes(chip) - 1U) / record_bytes(chip);
	period = chip->clocks_per_sample * samples;
	if (chip->next_drdy == 0U) {
		chip->next_drdy = ((clock->clock / period) + 1U) * period;
	}
	if (clock->clock >= chip->next_drdy) {
		stm32_gpio_drive(chip->drdy_port, chip->drdy_pin, !active_low);
		stm32_gpio_drive(chip->drdy_port, chip->drdy_pin, active_low);
		chip->next_drdy += period * (1U + ((clock->clock - chip->next_drdy) / period));
	}
	if ((chip->next_drdy - clock->clock) < clock->min_intr_interval) {
		clock->min_intr_interval = chip->next_drdy - clock->clock;
	}
}

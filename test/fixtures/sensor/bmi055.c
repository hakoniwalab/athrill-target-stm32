/* Bosch BMI055 register, FIFO and data-ready behavior (see bmi055.h). */
#include "bmi055.h"
#include "sensor_source.h"
#include "cpuemu_ops.h"
#include "peripheral/stm32_gpio.h"

#define REG_CHIP_ID		0x00U
#define REG_DATA_X_L		0x02U
#define REG_TEMP		0x08U
#define REG_INT_STATUS_1	0x0AU
#define REG_FIFO_STATUS		0x0EU
#define REG_RANGE		0x0FU
#define REG_SOFTRESET		0x14U
#define REG_ACC_INT_EN_1	0x17U
#define REG_ACC_INT_MAP_1	0x1AU
#define REG_ACC_INT_OUT_CTRL	0x20U
#define REG_ACC_FIFO_CONFIG_0	0x30U
#define REG_GYR_INT_EN_0	0x15U
#define REG_GYR_INT_EN_1	0x16U
#define REG_GYR_INT_MAP_1	0x18U
#define REG_GYR_FIFO_WM_ENABLE	0x1EU
#define REG_GYR_FIFO_CONFIG_0	0x3DU
#define REG_FIFO_CONFIG_1	0x3EU
#define REG_FIFO_DATA		0x3FU

#define ACC_ID			0xFAU
#define GYRO_ID			0x0FU
#define SOFTRESET_VALUE		0xB6U
#define FIFO_MODE_MASK		0xC0U /* stream mode is bit 6 (PX4) or bit 7 (AP) */
#define GRAVITY			9.80665F
#define RAD_TO_DEG		57.29577951F

static sint16 saturate(float32 v, float32 minimum, float32 maximum)
{
	if (v >= maximum) {
		return (sint16)maximum;
	}
	if (v <= minimum) {
		return (sint16)minimum;
	}
	return (sint16)((v < 0.0F) ? (v - 0.5F) : (v + 0.5F));
}

/* PX4 and AP convert Bosch x-left-up to body x-right-down after mounting. */
static void to_chip_axes(const Bmi055Type *chip, const float32 *frd, float32 *out)
{
	float32 x = frd[0];
	float32 y = frd[1];

	if (chip->rotation == BMI055_ROTATION_YAW_180) {
		x = -x;
		y = -y;
	}
	out[0] = x;
	out[1] = -y;
	out[2] = -frd[2];
}

static void raw_sample(const Bmi055Type *chip, sint16 *raw)
{
	static const float32 accel_lsb_per_g[16] = {
		0.0F, 0.0F, 0.0F, 1024.0F, 0.0F, 512.0F, 0.0F, 0.0F,
		256.0F, 0.0F, 0.0F, 0.0F, 128.0F, 0.0F, 0.0F, 0.0F
	};
	static const float32 gyro_lsb_per_dps[8] = {
		16.384F, 32.768F, 65.536F, 0.0F, 131.072F, 262.144F, 0.0F, 0.0F
	};
	SensorSampleType sample;
	float32 v[3];
	float32 scale;
	uint32 k;

	sensor_source_sample(&sample);
	to_chip_axes(chip, (chip->kind == BMI055_ACCEL) ? sample.acc : sample.gyro, v);
	if (chip->kind == BMI055_ACCEL) {
		scale = accel_lsb_per_g[chip->regs[REG_RANGE] & 0x0FU];
		if (scale == 0.0F) {
			scale = 128.0F;
		}
		scale /= GRAVITY;
		for (k = 0U; k < 3U; k++) {
			raw[k] = saturate(v[k] * scale, -2048.0F, 2047.0F);
		}
	}
	else {
		scale = gyro_lsb_per_dps[chip->regs[REG_RANGE] & 7U];
		if (scale == 0.0F) {
			scale = 16.384F;
		}
		scale *= RAD_TO_DEG;
		for (k = 0U; k < 3U; k++) {
			raw[k] = saturate(v[k] * scale, -32768.0F, 32767.0F);
		}
	}
}

static std_bool fifo_on(const Bmi055Type *chip)
{
	return (chip->regs[REG_FIFO_CONFIG_1] & FIFO_MODE_MASK) != 0U;
}

static uint32 fifo_capacity(const Bmi055Type *chip)
{
	return (chip->kind == BMI055_ACCEL) ? 32U : 100U;
}

static void fifo_restart(Bmi055Type *chip)
{
	chip->fifo_start = cpuemu_get_clock();
	chip->fifo_read_frames = 0U;
	chip->next_drdy = 0U;
}

static uint32 fifo_frames(const Bmi055Type *chip)
{
	uint64 made;
	uint64 available;

	if (!fifo_on(chip)) {
		return 0U;
	}
	made = (cpuemu_get_clock() - chip->fifo_start) / chip->clocks_per_sample;
	available = (made > chip->fifo_read_frames) ? (made - chip->fifo_read_frames) : 0U;
	return (available > fifo_capacity(chip)) ? fifo_capacity(chip) : (uint32)available;
}

static uint8 fifo_status(const Bmi055Type *chip)
{
	uint64 made = fifo_on(chip) ? (cpuemu_get_clock() - chip->fifo_start) / chip->clocks_per_sample : 0U;
	uint64 available = (made > chip->fifo_read_frames) ? made - chip->fifo_read_frames : 0U;
	uint8 overrun = (available > fifo_capacity(chip)) ? 0x80U : 0U;

	return overrun | (uint8)fifo_frames(chip);
}

static void make_record(Bmi055Type *chip)
{
	sint16 raw[3];
	uint32 k;

	raw_sample(chip, raw);
	for (k = 0U; k < 3U; k++) {
		uint16 value = (chip->kind == BMI055_ACCEL) ? (uint16)((sint32)raw[k] * 16) : (uint16)raw[k];

		chip->record[2U * k] = (uint8)value;
		chip->record[(2U * k) + 1U] = (uint8)(value >> 8);
	}
}

static uint8 fifo_byte(Bmi055Type *chip)
{
	uint32 pos;

	if (fifo_frames(chip) == 0U) {
		return 0U;
	}
	/* address remains FIFO_DATA; use its otherwise read-only backing byte as the record cursor. */
	pos = chip->regs[REG_FIFO_DATA] % 6U;
	if (pos == 0U) {
		make_record(chip);
	}
	chip->regs[REG_FIFO_DATA] = (uint8)((pos + 1U) % 6U);
	if (pos == 5U) {
		chip->fifo_read_frames++;
	}
	return chip->record[pos];
}

static uint8 data_register(Bmi055Type *chip, uint8 address)
{
	sint16 raw[3];
	uint32 offset = address - REG_DATA_X_L;
	uint16 value;

	raw_sample(chip, raw);
	value = (chip->kind == BMI055_ACCEL)
		? (uint16)((sint32)raw[offset / 2U] * 16) : (uint16)raw[offset / 2U];
	return ((offset & 1U) == 0U) ? (uint8)value : (uint8)(value >> 8);
}

static uint8 read_register(Bmi055Type *chip, uint8 address)
{
	if (address == REG_CHIP_ID) {
		return (chip->kind == BMI055_ACCEL) ? ACC_ID : GYRO_ID;
	}
	if ((address >= REG_DATA_X_L) && (address < (REG_DATA_X_L + 6U))) {
		return data_register(chip, address);
	}
	if (address == REG_TEMP && chip->kind == BMI055_ACCEL) {
		return (uint8)-6; /* 20 C: 23 C + 0.5 C/LSB */
	}
	if (address == REG_FIFO_STATUS) {
		return fifo_status(chip);
	}
	if (address == REG_FIFO_DATA) {
		return fifo_byte(chip);
	}
	if (address == REG_INT_STATUS_1 && chip->kind == BMI055_ACCEL) {
		return (fifo_frames(chip) >= (chip->regs[REG_ACC_FIFO_CONFIG_0] & 0x3FU)) ? 0x40U : 0U;
	}
	return chip->regs[address & 0x3FU];
}

static void reset_registers(Bmi055Type *chip)
{
	uint32 k;

	for (k = 0U; k < 64U; k++) {
		chip->regs[k] = 0U;
	}
	chip->regs[REG_CHIP_ID] = (chip->kind == BMI055_ACCEL) ? ACC_ID : GYRO_ID;
	chip->regs[REG_RANGE] = (chip->kind == BMI055_ACCEL) ? 0x03U : 0U;
}

static void write_register(Bmi055Type *chip, uint8 address, uint8 value)
{
	std_bool was_on = fifo_on(chip);
	uint8 fifo_config_0 = (chip->kind == BMI055_ACCEL) ? REG_ACC_FIFO_CONFIG_0 : REG_GYR_FIFO_CONFIG_0;

	if (address == REG_SOFTRESET && value == SOFTRESET_VALUE) {
		reset_registers(chip);
		fifo_restart(chip);
		return;
	}
	if ((address == REG_CHIP_ID) || (address == REG_FIFO_STATUS) || (address == REG_FIFO_DATA)) {
		return;
	}
	chip->regs[address & 0x3FU] = value;
	if ((address == fifo_config_0) || (address == REG_FIFO_CONFIG_1) || (!was_on && fifo_on(chip))) {
		chip->regs[REG_FIFO_DATA] = 0U;
		fifo_restart(chip);
	}
}

static void chip_select(void *p, std_bool selected)
{
	Bmi055Type *chip = (Bmi055Type *)p;

	(void)selected;
	chip->started = FALSE;
	chip->regs[REG_FIFO_DATA] = 0U;
}

static uint8 chip_exchange(void *p, uint8 mosi)
{
	Bmi055Type *chip = (Bmi055Type *)p;
	uint8 miso = 0U;

	if (!chip->started) {
		chip->started = TRUE;
		chip->reading = (mosi & 0x80U) != 0U;
		chip->address = mosi & 0x3FU;
		return 0U;
	}
	if (chip->reading) {
		miso = read_register(chip, chip->address);
	}
	else {
		write_register(chip, chip->address, mosi);
	}
	if (chip->address != REG_FIFO_DATA) {
		chip->address = (chip->address + 1U) & 0x3FU;
	}
	return miso;
}

const Stm32SpiChipOps bmi055_ops = { chip_select, chip_exchange };

void bmi055_reset(Bmi055Type *chip, Bmi055KindType kind, uint64 cpu_hz,
	Bmi055RotationType rotation, uint32 drdy_port, uint32 drdy_pin)
{
	chip->kind = kind;
	chip->rotation = rotation;
	chip->drdy_port = drdy_port;
	chip->drdy_pin = drdy_pin;
	chip->clocks_per_sample = cpu_hz / 2000U;
	chip->started = FALSE;
	chip->reading = FALSE;
	chip->address = 0U;
	reset_registers(chip);
	fifo_restart(chip);
	stm32_gpio_drive(drdy_port, drdy_pin, TRUE);
}

static std_bool interrupt_enabled(const Bmi055Type *chip)
{
	if (chip->kind == BMI055_ACCEL) {
		return ((chip->regs[REG_ACC_INT_EN_1] & 0x40U) != 0U)
			&& ((chip->regs[REG_ACC_INT_MAP_1] & 0x02U) != 0U);
	}
	return ((chip->regs[REG_GYR_INT_EN_0] & 0x40U) != 0U)
		&& ((chip->regs[REG_GYR_INT_MAP_1] & 0x04U) != 0U)
		&& ((chip->regs[REG_GYR_FIFO_WM_ENABLE] & 0x80U) != 0U);
}

void bmi055_tick(Bmi055Type *chip, DeviceClockType *clock)
{
	uint8 cfg0 = chip->regs[(chip->kind == BMI055_ACCEL) ? REG_ACC_FIFO_CONFIG_0 : REG_GYR_FIFO_CONFIG_0];
	uint32 watermark = cfg0 & ((chip->kind == BMI055_ACCEL) ? 0x3FU : 0x7FU);
	uint64 period;

	stm32_gpio_drive(chip->drdy_port, chip->drdy_pin, TRUE); /* both board pins are active low */
	if (!fifo_on(chip) || !interrupt_enabled(chip)) {
		return;
	}
	if (watermark == 0U) {
		watermark = 1U;
	}
	period = chip->clocks_per_sample * watermark;
	if (chip->next_drdy == 0U) {
		chip->next_drdy = ((clock->clock / period) + 1U) * period;
	}
	if (clock->clock >= chip->next_drdy) {
		stm32_gpio_drive(chip->drdy_port, chip->drdy_pin, FALSE);
		stm32_gpio_drive(chip->drdy_port, chip->drdy_pin, TRUE);
		chip->next_drdy += period * (1U + ((clock->clock - chip->next_drdy) / period));
	}
	if ((chip->next_drdy - clock->clock) < clock->min_intr_interval) {
		clock->min_intr_interval = chip->next_drdy - clock->clock;
	}
}

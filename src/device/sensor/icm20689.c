/*
 * The ICM-20689 on SPI (see icm20689.h). Register addresses and bits from
 * the datasheet's register map (DS-000143, section 8).
 */
#include "icm20689.h"
#include "sensor_source.h"
#include "cpuemu_ops.h"
#include "peripheral/stm32_gpio.h"

#define REG_SMPLRT_DIV		0x19U
#define REG_CONFIG		0x1AU
#define REG_GYRO_CONFIG		0x1BU
#define REG_ACCEL_CONFIG	0x1CU
#define REG_FIFO_EN		0x23U
#define REG_INT_PIN_CFG		0x37U
#define REG_INT_ENABLE		0x38U
#define REG_INT_STATUS		0x3AU
#define REG_ACCEL_XOUT_H	0x3BU	/* to GYRO_ZOUT_L 0x48 */
#define REG_TEMP_OUT_H		0x41U
#define REG_GYRO_XOUT_H		0x43U
#define REG_SIGNAL_PATH_RESET	0x68U
#define REG_USER_CTRL		0x6AU
#define REG_PWR_MGMT_1		0x6BU
#define REG_FIFO_COUNTH		0x72U
#define REG_FIFO_COUNTL		0x73U
#define REG_FIFO_R_W		0x74U
#define REG_WHO_AM_I		0x75U

#define WHO_AM_I_VALUE		0x98U
#define PWR_MGMT_1_RESET	0x40U	/* SLEEP */

#define FIFO_EN_TEMP		(1U << 7)
#define FIFO_EN_XG		(1U << 6)
#define FIFO_EN_YG		(1U << 5)
#define FIFO_EN_ZG		(1U << 4)
#define FIFO_EN_ACCEL		(1U << 3)
#define USER_CTRL_FIFO_EN	(1U << 6)
#define USER_CTRL_FIFO_RST	(1U << 2)
#define USER_CTRL_SIG_COND_RST	(1U << 0)
#define PWR_MGMT_1_RESET_BIT	(1U << 7)
#define PWR_MGMT_1_SLEEP	(1U << 6)
#define INT_PIN_CFG_INT_LEVEL	(1U << 7)	/* active low */
#define INT_ENABLE_DATA_RDY	(1U << 0)

#define GRAVITY			9.80665F
#define RAD_TO_DEG		57.29577951F
#define TEMP_SENSITIVITY	326.8F	/* LSB per degree C */
#define TEMP_OFFSET		25.0F

/* The sample in the chip's axes: x forward, y left, z up, before the board's rotation. */
typedef struct {
	sint16 acc[3];
	sint16 temp;
	sint16 gyro[3];
} RawSampleType;

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

/*
 * Body front-right-down to the chip's axes. PX4 reads the chip as
 * (x, -y, -z) and then applies the board's rotation; this undoes both.
 */
static void to_chip_axes(const Icm20689Type *chip, const float32 *frd, float32 *out)
{
	float32 v[3];

	if (chip->rotation == ICM20689_ROTATION_YAW_90) {
		/* yaw 90: body = (-v_y, v_x, v_z) */
		v[0] = frd[1];
		v[1] = -frd[0];
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

static void raw_sample(const Icm20689Type *chip, RawSampleType *raw)
{
	static const float32 acc_lsb_per_g[4] = { 16384.0F, 8192.0F, 4096.0F, 2048.0F };
	static const float32 gyro_lsb_per_dps[4] = { 131.0F, 65.5F, 32.8F, 16.4F };
	SensorSampleType s;
	float32 acc[3];
	float32 gyro[3];
	float32 acc_scale = acc_lsb_per_g[(chip->regs[REG_ACCEL_CONFIG] >> 3) & 3U] / GRAVITY;
	float32 gyro_scale = gyro_lsb_per_dps[(chip->regs[REG_GYRO_CONFIG] >> 3) & 3U] * RAD_TO_DEG;
	uint32 k;

	sensor_source_sample(&s);
	/* the accelerometer measures specific force: at rest, 1 g upwards */
	to_chip_axes(chip, s.acc, acc);
	to_chip_axes(chip, s.gyro, gyro);
	for (k = 0U; k < 3U; k++) {
		raw->acc[k] = saturate(acc[k] * acc_scale);
		raw->gyro[k] = saturate(gyro[k] * gyro_scale);
	}
	raw->temp = saturate((s.temperature - TEMP_OFFSET) * TEMP_SENSITIVITY);
}

/* The sample rate in CPU clocks per sample: 8 kHz with the DLPF bypassed, else 1 kHz / (1 + SMPLRT_DIV). */
static uint64 sample_clocks(const Icm20689Type *chip)
{
	uint32 dlpf = chip->regs[REG_CONFIG] & 7U;

	if ((dlpf == 0U) || (dlpf == 7U)) {
		return chip->clocks_per_sample;
	}
	return chip->clocks_per_sample * 8U * (1U + chip->regs[REG_SMPLRT_DIV]);
}

static std_bool sampling(const Icm20689Type *chip)
{
	return (chip->regs[REG_PWR_MGMT_1] & PWR_MGMT_1_SLEEP) == 0U;
}

/* Bytes per FIFO record. */
static uint32 record_bytes(const Icm20689Type *chip)
{
	uint8 en = chip->regs[REG_FIFO_EN];
	uint32 n = 0U;

	n += ((en & FIFO_EN_ACCEL) != 0U) ? 6U : 0U;
	n += ((en & FIFO_EN_TEMP) != 0U) ? 2U : 0U;
	n += ((en & FIFO_EN_XG) != 0U) ? 2U : 0U;
	n += ((en & FIFO_EN_YG) != 0U) ? 2U : 0U;
	n += ((en & FIFO_EN_ZG) != 0U) ? 2U : 0U;
	return n;
}

static std_bool fifo_on(const Icm20689Type *chip)
{
	return ((chip->regs[REG_USER_CTRL] & USER_CTRL_FIFO_EN) != 0U) && (record_bytes(chip) != 0U) && sampling(chip);
}

/* Empties the FIFO; it fills from now on. */
static void fifo_restart(Icm20689Type *chip)
{
	chip->fifo_start = cpuemu_get_clock();
	chip->fifo_taken = 0U;
	chip->fifo_read_bytes = 0U;
	chip->fifo_overflow = FALSE;
}

/* The bytes in the FIFO now (at most its size). */
static uint32 fifo_count(Icm20689Type *chip)
{
	uint64 now = cpuemu_get_clock();
	uint64 bytes;

	if (!fifo_on(chip)) {
		bytes = chip->fifo_taken * record_bytes(chip);
		return (bytes > chip->fifo_read_bytes) ? (uint32)(bytes - chip->fifo_read_bytes) : 0U;
	}
	chip->fifo_taken = (now - chip->fifo_start) / sample_clocks(chip);
	bytes = (chip->fifo_taken * record_bytes(chip)) - chip->fifo_read_bytes;
	if (bytes > ICM20689_FIFO_BYTES) {
		chip->fifo_overflow = TRUE;
		bytes = ICM20689_FIFO_BYTES;
	}
	return (uint32)bytes;
}

/* The next FIFO byte: records of accel (H, L per axis), temperature, gyro x, y, z, as enabled. */
static uint8 fifo_byte(Icm20689Type *chip)
{
	uint32 rec = record_bytes(chip);
	uint32 pos;
	uint8 layout[14];
	uint32 n = 0U;
	uint8 en = chip->regs[REG_FIFO_EN];
	RawSampleType raw;
	uint32 k;

	if ((rec == 0U) || (fifo_count(chip) == 0U)) {
		return 0U;
	}
	raw_sample(chip, &raw);
	if ((en & FIFO_EN_ACCEL) != 0U) {
		for (k = 0U; k < 3U; k++) {
			layout[n++] = (uint8)((uint16)raw.acc[k] >> 8);
			layout[n++] = (uint8)raw.acc[k];
		}
	}
	if ((en & FIFO_EN_TEMP) != 0U) {
		layout[n++] = (uint8)((uint16)raw.temp >> 8);
		layout[n++] = (uint8)raw.temp;
	}
	for (k = 0U; k < 3U; k++) {
		if ((en & (FIFO_EN_XG >> k)) != 0U) {
			layout[n++] = (uint8)((uint16)raw.gyro[k] >> 8);
			layout[n++] = (uint8)raw.gyro[k];
		}
	}
	pos = (uint32)(chip->fifo_read_bytes % rec);
	chip->fifo_read_bytes++;
	return layout[pos];
}

static uint8 data_register(const Icm20689Type *chip, uint8 address)
{
	RawSampleType raw;
	sint16 v;

	raw_sample(chip, &raw);
	if (address < REG_TEMP_OUT_H) {
		v = raw.acc[(address - REG_ACCEL_XOUT_H) / 2U];
	}
	else if (address < REG_GYRO_XOUT_H) {
		v = raw.temp;
	}
	else {
		v = raw.gyro[(address - REG_GYRO_XOUT_H) / 2U];
	}
	return ((address & 1U) == (REG_ACCEL_XOUT_H & 1U)) ? (uint8)((uint16)v >> 8) : (uint8)v;
}

static uint8 read_register(Icm20689Type *chip, uint8 address)
{
	switch (address) {
	case REG_FIFO_COUNTH:
		/* reading the high byte latches the count for the low byte */
		{
			uint32 count = fifo_count(chip);

			chip->regs[REG_FIFO_COUNTL] = (uint8)count;
			return (uint8)(count >> 8);
		}
	case REG_FIFO_COUNTL:
		return chip->regs[REG_FIFO_COUNTL];
	case REG_FIFO_R_W:
		return fifo_byte(chip);
	case REG_INT_STATUS:
		return sampling(chip) ? 1U : 0U;	/* DATA_RDY_INT */
	case REG_WHO_AM_I:
		return WHO_AM_I_VALUE;
	default:
		if ((address >= REG_ACCEL_XOUT_H) && (address <= (REG_GYRO_XOUT_H + 5U))) {
			return data_register(chip, address);
		}
		return chip->regs[address & 0x7FU];
	}
}

static void reset_registers(Icm20689Type *chip)
{
	uint32 i;

	for (i = 0U; i < 128U; i++) {
		chip->regs[i] = 0U;
	}
	chip->regs[REG_PWR_MGMT_1] = PWR_MGMT_1_RESET;
	chip->regs[REG_WHO_AM_I] = WHO_AM_I_VALUE;
}

static void write_register(Icm20689Type *chip, uint8 address, uint8 value)
{
	std_bool was_on = fifo_on(chip);

	(void)fifo_count(chip);
	switch (address) {
	case REG_PWR_MGMT_1:
		if ((value & PWR_MGMT_1_RESET_BIT) != 0U) {
			reset_registers(chip);
			fifo_restart(chip);
			return;
		}
		break;
	case REG_USER_CTRL:
		if ((value & USER_CTRL_FIFO_RST) != 0U) {
			fifo_restart(chip);
		}
		value &= (uint8)~(USER_CTRL_FIFO_RST | USER_CTRL_SIG_COND_RST);	/* self-clearing */
		break;
	case REG_SIGNAL_PATH_RESET:
		value = 0U;	/* self-clearing */
		break;
	case REG_FIFO_COUNTH:
	case REG_FIFO_COUNTL:
	case REG_INT_STATUS:
	case REG_WHO_AM_I:
		return;
	case REG_FIFO_R_W:
		return;
	default:
		break;
	}
	chip->regs[address & 0x7FU] = value;
	if (!was_on && fifo_on(chip)) {
		fifo_restart(chip);
	}
}

static void chip_select(void *p, std_bool selected)
{
	Icm20689Type *chip = (Icm20689Type *)p;

	(void)selected;
	chip->started = FALSE;
}

static uint8 chip_exchange(void *p, uint8 mosi)
{
	Icm20689Type *chip = (Icm20689Type *)p;
	uint8 miso = 0U;

	if (!chip->started) {
		chip->started = TRUE;
		chip->reading = ((mosi & 0x80U) != 0U);
		chip->address = (uint8)(mosi & 0x7FU);
		return 0U;
	}
	if (chip->reading) {
		miso = read_register(chip, chip->address);
	}
	else {
		write_register(chip, chip->address, mosi);
	}
	/* auto-increment, but for FIFO_R_W */
	if (chip->address != REG_FIFO_R_W) {
		chip->address = (uint8)((chip->address + 1U) & 0x7FU);
	}
	return miso;
}

const Stm32SpiChipOps icm20689_ops = { chip_select, chip_exchange };

void icm20689_reset(Icm20689Type *chip, uint64 cpu_hz, Icm20689RotationType rotation, uint32 drdy_port, uint32 drdy_pin)
{
	reset_registers(chip);
	chip->rotation = rotation;
	chip->drdy_port = drdy_port;
	chip->drdy_pin = drdy_pin;
	chip->clocks_per_sample = cpu_hz / 8000U;
	chip->started = FALSE;
	chip->reading = FALSE;
	chip->address = 0U;
	chip->fifo_start = 0U;
	chip->fifo_taken = 0U;
	chip->fifo_read_bytes = 0U;
	chip->fifo_overflow = FALSE;
	chip->next_sample = 0U;
}

void icm20689_tick(Icm20689Type *chip, DeviceClockType *clock)
{
	std_bool active_low = ((chip->regs[REG_INT_PIN_CFG] & INT_PIN_CFG_INT_LEVEL) != 0U);
	uint64 period;

	if (((chip->regs[REG_INT_ENABLE] & INT_ENABLE_DATA_RDY) == 0U) || !sampling(chip)) {
		return;
	}
	period = sample_clocks(chip);
	/* the pin rests inactive between pulses */
	stm32_gpio_drive(chip->drdy_port, chip->drdy_pin, active_low);
	if (chip->next_sample == 0U) {
		chip->next_sample = ((clock->clock / period) + 1U) * period;
	}
	if (clock->clock >= chip->next_sample) {
		/* a pulse: active, then inactive again */
		stm32_gpio_drive(chip->drdy_port, chip->drdy_pin, !active_low);
		stm32_gpio_drive(chip->drdy_port, chip->drdy_pin, active_low);
		chip->next_sample += period * (1U + ((clock->clock - chip->next_sample) / period));
	}
	if ((chip->next_sample - clock->clock) < clock->min_intr_interval) {
		clock->min_intr_interval = chip->next_sample - clock->clock;
	}
}

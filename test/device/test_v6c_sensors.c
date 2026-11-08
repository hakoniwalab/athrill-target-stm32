/* Register/FIFO tests for the Pixhawk 6C sensor chips and MS5611 adapter. */
#include <stdio.h>
#include <string.h>
#include "sensor/bmi055.h"
#include "sensor/icm42688p.h"
#include "sensor/ms5611.h"
#include "sensor_source.h"

#define CPU_HZ 480000000ULL

static uint64 now;
static int failures;
static int checks;

uint64 cpuemu_get_clock(void)
{
	return now;
}

void stm32_gpio_drive(uint32 port, uint32 pin, std_bool level)
{
	(void)port;
	(void)pin;
	(void)level;
}

void sensor_source_sample(SensorSampleType *sample)
{
	memset(sample, 0, sizeof(*sample));
	sample->acc[2] = -9.80665F;
	sample->gyro[0] = 0.01F;
	sample->mag[0] = 0.3F;
	sample->abs_pressure = 1013.25F;
	sample->temperature = 20.0F;
}

#define CHECK_EQ(what, got, want) check_eq(__LINE__, what, (long long)(got), (long long)(want))

static void check_eq(int line, const char *what, long long got, long long want)
{
	checks++;
	if (got != want) {
		failures++;
		printf("FAIL line %d: %s = %lld, want %lld\n", line, what, got, want);
	}
}

static void spi_write(const Stm32SpiChipOps *ops, void *chip, uint8 reg, uint8 value)
{
	ops->select(chip, TRUE);
	(void)ops->exchange(chip, reg);
	(void)ops->exchange(chip, value);
	ops->select(chip, FALSE);
}

static uint8 spi_read(const Stm32SpiChipOps *ops, void *chip, uint8 reg)
{
	uint8 value;

	ops->select(chip, TRUE);
	(void)ops->exchange(chip, reg | 0x80U);
	value = ops->exchange(chip, 0U);
	ops->select(chip, FALSE);
	return value;
}

static void spi_read_bytes(const Stm32SpiChipOps *ops, void *chip, uint8 reg, uint8 *data, uint32 count)
{
	uint32 k;

	ops->select(chip, TRUE);
	(void)ops->exchange(chip, reg | 0x80U);
	for (k = 0U; k < count; k++) {
		data[k] = ops->exchange(chip, 0U);
	}
	ops->select(chip, FALSE);
}

static void test_bmi055(void)
{
	Bmi055Type accel;
	Bmi055Type gyro;
	uint8 data[6];
	sint16 z;

	now = 0U;
	bmi055_reset(&accel, BMI055_ACCEL, CPU_HZ, BMI055_ROTATION_YAW_180, 4U, 4U);
	CHECK_EQ("BMI055 accel ID", spi_read(&bmi055_ops, &accel, 0x00U), 0xFAU);
	spi_write(&bmi055_ops, &accel, 0x0FU, 0x0CU);
	spi_write(&bmi055_ops, &accel, 0x30U, 5U);
	spi_write(&bmi055_ops, &accel, 0x3EU, 0x40U);
	now += (CPU_HZ / 2000U) * 5U;
	CHECK_EQ("BMI055 accel FIFO frames", spi_read(&bmi055_ops, &accel, 0x0EU) & 0x7FU, 5U);
	spi_read_bytes(&bmi055_ops, &accel, 0x3FU, data, sizeof(data));
	z = (sint16)(((uint16)data[5] << 8) | data[4]);
	CHECK_EQ("BMI055 accel one g, left aligned", z, 128 * 16);

	now = 0U;
	bmi055_reset(&gyro, BMI055_GYRO, CPU_HZ, BMI055_ROTATION_YAW_180, 4U, 5U);
	CHECK_EQ("BMI055 gyro ID", spi_read(&bmi055_ops, &gyro, 0x00U), 0x0FU);
	spi_write(&bmi055_ops, &gyro, 0x3EU, 0x80U); /* ArduPilot FIFO mode */
	now += CPU_HZ / 2000U;
	CHECK_EQ("BMI055 gyro FIFO frame", spi_read(&bmi055_ops, &gyro, 0x0EU) & 0x7FU, 1U);
}

static void test_icm42688p(void)
{
	Icm42688pType imu;
	uint8 data[20];
	uint16 count;

	now = 0U;
	icm42688p_reset(&imu, CPU_HZ, ICM42688P_ROTATION_YAW_270, 4U, 6U);
	CHECK_EQ("ICM42688P WHO_AM_I", spi_read(&icm42688p_ops, &imu, 0x75U), 0x47U);
	spi_write(&icm42688p_ops, &imu, 0x4CU, 0x30U); /* big-endian byte count and data */
	spi_write(&icm42688p_ops, &imu, 0x4EU, 0x0FU);
	spi_write(&icm42688p_ops, &imu, 0x5FU, 0x17U); /* high resolution, temp, accel, gyro */
	now += (CPU_HZ / 8000U) * 10U;
	count = ((uint16)spi_read(&icm42688p_ops, &imu, 0x2EU) << 8)
		| spi_read(&icm42688p_ops, &imu, 0x2FU);
	CHECK_EQ("ICM42688P byte count", count, 200U);
	spi_read_bytes(&icm42688p_ops, &imu, 0x30U, data, 20U);
	CHECK_EQ("ICM42688P high-resolution FIFO header", data[0], 0x78U);

	spi_write(&icm42688p_ops, &imu, 0x4BU, 0x02U); /* flush */
	spi_write(&icm42688p_ops, &imu, 0x4CU, 0xC0U); /* AP: little-endian record count */
	spi_write(&icm42688p_ops, &imu, 0x5FU, 0x07U); /* 16-byte records */
	now += (CPU_HZ / 8000U) * 4U;
	count = spi_read(&icm42688p_ops, &imu, 0x2EU)
		| ((uint16)spi_read(&icm42688p_ops, &imu, 0x2FU) << 8);
	CHECK_EQ("ICM42688P record count", count, 4U);
	spi_read_bytes(&icm42688p_ops, &imu, 0x30U, data, 16U);
	CHECK_EQ("ICM42688P standard FIFO header", data[0], 0x68U);
}

static void test_ms5611_i2c(void)
{
	Ms5611Type baro;
	uint16 c1;

	ms5611_reset(&baro);
	CHECK_EQ("MS5611 I2C write ACK", ms5611_i2c_ops.start(&baro, FALSE), TRUE);
	ms5611_i2c_ops.write(&baro, 0xA2U);
	ms5611_i2c_ops.stop(&baro);
	CHECK_EQ("MS5611 I2C read ACK", ms5611_i2c_ops.start(&baro, TRUE), TRUE);
	c1 = (uint16)ms5611_i2c_ops.read(&baro) << 8;
	c1 |= ms5611_i2c_ops.read(&baro);
	CHECK_EQ("MS5611 calibration C1", c1, 40127U);
}

int main(void)
{
	test_bmi055();
	test_icm42688p();
	test_ms5611_i2c();
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}

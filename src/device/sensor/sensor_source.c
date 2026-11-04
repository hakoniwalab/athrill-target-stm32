/*
 * The sensor source (see sensor_source.h). The register offsets are those
 * of the Hakoniwa sensor device (hakoniwa-drone-pro,
 * devices/common/hako_sensor_device_regs.h).
 */
#include "sensor_source.h"
#include "bus.h"
#include "mpu.h"
#include "cpuemu_ops.h"
#include <string.h>
#include <math.h>

#define DEFAULT_BASE		0x60000000U

#define REG_ID			0x000U
#define REG_ACC_X		0x050U	/* then ACC_Y, ACC_Z, GYRO_X..Z, MAG_X..Z, ABS_PRESSURE */
#define REG_ABS_PRESSURE	0x074U
#define REG_TEMPERATURE		0x080U
#define REG_GPS_TIME_LO		0x100U
#define REG_GPS_TIME_HI		0x104U
#define REG_GPS_LAT		0x108U	/* then LON, ALT, EPH, EPV, VEL, VN, VE, VD, COG, YAW, FIX_TYPE, SATELLITES */
#define REG_GPS_FIX_TYPE	0x134U
#define REG_GPS_SATELLITES	0x138U
#define REG_CONTROL0		0x200U
#define REG_MODE		0x240U
#define REG_COMMIT		0x24CU
#define ID_VALUE		0x4f4b4148U	/* "HAKO" */
#define ACTUATORS_MAX		16U
#define MODE_ARMED		0x80U	/* MAV_MODE_FLAG_SAFETY_ARMED */

static uint32 base = DEFAULT_BASE;
static std_bool looked;
static std_bool present;
static uint32 cpu_mhz = 216U;
static uint32 bench;

/*
 * Without the device the chips report constant values (PX4 takes them as
 * stale, so in SIH they stay out of its data path), or, with
 * DEVICE_CONFIG_STM32_SENSOR_BENCH 1, a bench: a vehicle at rest, level, facing north, at
 * Tokyo Station (35.681236 N, 139.767125 E, 40 m), with the Earth's field
 * there (about 0.47 gauss, inclination 49 degrees, declination -7.5
 * degrees) and a GPS fix. Gaussian noise from a fixed-seed generator
 * stands in for the sensors' own noise (PX4 takes a sensor whose value
 * never changes as stale): larger than one step of each chip's output
 * (ICM-20689 0.005 m/s^2 and 0.001 rad/s, IST8310 0.003 gauss), drawn in
 * the order the chips ask for samples, so runs stay identical.
 */
#define BENCH_LAT	356812360	/* degE7 */
#define BENCH_LON	1397671250
#define BENCH_ALT	40000		/* mm */
#define BENCH_ACC_NOISE		0.02F	/* m/s^2, RMS */
#define BENCH_GYRO_NOISE	0.003F	/* rad/s */
#define BENCH_MAG_NOISE		0.003F	/* gauss */

static uint64 noise_state;

/* A standard normal value (Box-Muller on xorshift64). */
static float32 bench_noise(void)
{
	float64 u[2];
	uint32 k;

	for (k = 0U; k < 2U; k++) {
		noise_state ^= noise_state << 13;
		noise_state ^= noise_state >> 7;
		noise_state ^= noise_state << 17;
		u[k] = ((float64)(noise_state >> 11) + 0.5) / 9007199254740992.0;
	}
	return (float32)(sqrt(-2.0 * log(u[0])) * cos(6.283185307179586 * u[1]));
}

void sensor_source_reset(void)
{
	base = DEFAULT_BASE;
	(void)cpuemu_get_devcfg_value_hex("DEVICE_CONFIG_STM32_SENSOR_DEVICE", &base);
	cpu_mhz = 216U;
	(void)cpuemu_get_devcfg_value("DEVICE_CPU_FREQ", &cpu_mhz);
	noise_state = 0x2545F4914F6CDD1DULL;
	bench = 0U;
	(void)cpuemu_get_devcfg_value("DEVICE_CONFIG_STM32_SENSOR_BENCH", &bench);
	looked = FALSE;
	present = FALSE;
}

/* The device is mapped (memory.txt DEV) and answers with its ID. */
static std_bool device_present(void)
{
	uint32 i;
	uint32 id = 0U;

	if (looked) {
		return present;
	}
	looked = TRUE;
	for (i = 0U; i < mpu_address_map.dynamic_map_num; i++) {
		if ((uint32)mpu_address_map.dynamic_map[i].start == base) {
			present = (bus_get_data32(0U, base + REG_ID, &id) == STD_E_OK) && (id == ID_VALUE);
		}
	}
	return present;
}

static uint32 word(uint32 offset)
{
	uint32 value = 0U;

	(void)bus_get_data32(0U, base + offset, &value);
	return value;
}

static float32 real(uint32 offset)
{
	uint32 bits = word(offset);
	float32 value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

void sensor_source_sample(SensorSampleType *sample)
{
	uint32 k;

	if (!device_present() && (bench == 0U)) {
		memset(sample, 0, sizeof(*sample));
		sample->acc[2] = -9.80665F;
		sample->mag[0] = 0.21F;
		sample->mag[2] = 0.43F;
		sample->abs_pressure = 1013.25F;
		sample->temperature = 20.0F;
		return;
	}
	if (!device_present()) {
		memset(sample, 0, sizeof(*sample));
		sample->acc[2] = -9.80665F;
		sample->mag[0] = 0.307F;
		sample->mag[1] = -0.040F;
		sample->mag[2] = 0.355F;
		for (k = 0U; k < 3U; k++) {
			sample->acc[k] += BENCH_ACC_NOISE * bench_noise();
			sample->gyro[k] = BENCH_GYRO_NOISE * bench_noise();
			sample->mag[k] += BENCH_MAG_NOISE * bench_noise();
		}
		sample->abs_pressure = 1012.77F;	/* 40 m above sea level */
		sample->temperature = 20.0F;
		return;
	}
	for (k = 0U; k < 3U; k++) {
		sample->acc[k] = real(REG_ACC_X + (4U * k));
		sample->gyro[k] = real(REG_ACC_X + 12U + (4U * k));
		sample->mag[k] = real(REG_ACC_X + 24U + (4U * k));
	}
	sample->abs_pressure = real(REG_ABS_PRESSURE);
	sample->temperature = real(REG_TEMPERATURE);
}

std_bool sensor_source_gps(GpsSampleType *gps)
{
	if (!device_present() && (bench == 0U)) {
		return FALSE;
	}
	if (!device_present()) {
		memset(gps, 0, sizeof(*gps));
		gps->time_usec = (cpuemu_get_clock() / cpu_mhz) + 1U;
		gps->lat = BENCH_LAT;
		gps->lon = BENCH_LON;
		gps->alt = BENCH_ALT;
		gps->eph = 100U;
		gps->epv = 150U;
		gps->cog = 0U;
		gps->fix_type = 3U;
		gps->satellites = 12U;
		return TRUE;
	}
	gps->time_usec = ((uint64)word(REG_GPS_TIME_HI) << 32) | word(REG_GPS_TIME_LO);
	if (gps->time_usec == 0U) {
		return FALSE;
	}
	gps->lat = (sint32)word(REG_GPS_LAT);
	gps->lon = (sint32)word(REG_GPS_LAT + 4U);
	gps->alt = (sint32)word(REG_GPS_LAT + 8U);
	gps->eph = word(REG_GPS_LAT + 12U);
	gps->epv = word(REG_GPS_LAT + 16U);
	gps->vel = word(REG_GPS_LAT + 20U);
	gps->vn = (sint32)word(REG_GPS_LAT + 24U);
	gps->ve = (sint32)word(REG_GPS_LAT + 28U);
	gps->vd = (sint32)word(REG_GPS_LAT + 32U);
	gps->cog = word(REG_GPS_LAT + 36U);
	gps->fix_type = word(REG_GPS_FIX_TYPE);
	gps->satellites = word(REG_GPS_SATELLITES);
	return TRUE;
}

void sensor_source_actuators(const float32 *controls, uint32 count)
{
	uint32 k;
	uint32 bits;

	if (!device_present()) {
		return;
	}
	for (k = 0U; (k < count) && (k < ACTUATORS_MAX); k++) {
		memcpy(&bits, &controls[k], sizeof(bits));
		(void)bus_put_data32(0U, base + REG_CONTROL0 + (4U * k), bits);
	}
	(void)bus_put_data32(0U, base + REG_MODE, MODE_ARMED);
	(void)bus_put_data32(0U, base + REG_COMMIT, 1U);
}

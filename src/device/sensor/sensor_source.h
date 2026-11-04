/*
 * Where the sensor chips get their samples: the Hakoniwa sensor device
 * (an Athrill EXDEV of hakoniwa-drone-pro, devices/athrill), mapped by
 * memory.txt at DEVICE_CONFIG_STM32_SENSOR_DEVICE (0x60000000 by default)
 * and read through the bus. It holds the latest hil_sensor and hil_gps
 * samples (or a vehicle at rest in its test mode) and takes the motor
 * outputs for hil_actuator.
 *
 * Without the device, the chips see a bench: a vehicle at rest at Tokyo
 * Station (level, facing north, 1 g upwards, the Earth's field there, the
 * pressure 40 m above sea level at 20 degrees C), with a small dither that
 * repeats with the virtual time, and a 3D GPS fix there.
 *
 * Units and axes are those of HIL_SENSOR: m/s^2, rad/s, gauss, hPa,
 * degrees C, body front-right-down.
 */
#ifndef ATHRILL_STM32_SENSOR_SOURCE_H
#define ATHRILL_STM32_SENSOR_SOURCE_H

#include "std_types.h"

typedef struct {
	float32 acc[3];
	float32 gyro[3];
	float32 mag[3];
	float32 abs_pressure;
	float32 temperature;
} SensorSampleType;

typedef struct {
	sint32 lat;		/* degE7 */
	sint32 lon;		/* degE7 */
	sint32 alt;		/* mm above MSL */
	uint32 eph;		/* cm */
	uint32 epv;		/* cm */
	uint32 vel;		/* cm/s */
	sint32 vn;		/* cm/s */
	sint32 ve;
	sint32 vd;
	uint32 cog;		/* cdeg */
	uint32 fix_type;
	uint32 satellites;
	uint64 time_usec;
} GpsSampleType;

/* Reads the device configuration; looks for the sensor device. */
void sensor_source_reset(void);
/* The latest sensor sample. */
void sensor_source_sample(SensorSampleType *sample);
/* The latest GPS sample; FALSE if none yet. */
std_bool sensor_source_gps(GpsSampleType *gps);
/* The motor outputs (0..1 each) for hil_actuator. */
void sensor_source_actuators(const float32 *controls, uint32 count);

#endif

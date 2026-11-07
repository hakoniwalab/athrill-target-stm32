#ifndef ATHRILL_STM32_FMU_V6C_H
#define ATHRILL_STM32_FMU_V6C_H

#include "std_types.h"
#include "cpu.h"
#include "std_device_ops.h"

void fmu_v6c_attach(void);
void fmu_v6c_tick(DeviceClockType *clock);
void fmu_v6c_bmi055_accel_attach(void);
void fmu_v6c_bmi055_gyro_attach(void);
void fmu_v6c_icm42688p_attach(void);
std_bool fmu_v6c_sdcard_attach(void);

#endif /* ATHRILL_STM32_FMU_V6C_H */

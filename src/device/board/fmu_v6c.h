#ifndef ATHRILL_STM32_FMU_V6C_H
#define ATHRILL_STM32_FMU_V6C_H

#include "std_types.h"
#include "cpu.h"
#include "std_device_ops.h"

void fmu_v6c_attach(void);
void fmu_v6c_tick(DeviceClockType *clock);

#endif /* ATHRILL_STM32_FMU_V6C_H */

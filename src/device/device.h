#ifndef ATHRILL_STM32_DEVICE_H
#define ATHRILL_STM32_DEVICE_H
#include "std_types.h"
#include "cpu.h"
#include "mpu.h"
#include "std_device_ops.h"
void device_raise_int(uint16 intno);
#endif

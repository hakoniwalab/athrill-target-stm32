#ifndef ATHRILL_STM32_DEVICE_H
#define ATHRILL_STM32_DEVICE_H
#include "std_types.h"
#include "cpu.h"
#include "mpu.h"
#include "std_device_ops.h"
void device_raise_int(uint16 intno);
/* Set by every device register access: ends a batch of instructions (cpu_supply_clocks). */
extern std_bool stm32_device_accessed;
/* Whether interrupt intno is active (its handler running or preempted). */
std_bool intc_irq_active(uint32 intno);
#endif

#ifndef ATHRILL_STM32_DEVICE_H
#define ATHRILL_STM32_DEVICE_H
#include "std_types.h"
#include "cpu.h"
#include "mpu.h"
#include "std_device_ops.h"
void device_raise_int(uint16 intno);
/*
 * Set by a device register access that may change when the next interrupt
 * comes (writes, and reads with side effects such as a UART's RDR): ends a
 * batch of instructions (cpu_supply_clocks), so the devices tick and report
 * their next event again. Other reads only bring a device up to the clock;
 * the batch already ends at the next event the devices reported.
 */
extern std_bool stm32_device_accessed;
/* Whether interrupt intno is active (its handler running or preempted). */
std_bool intc_irq_active(uint32 intno);
#endif

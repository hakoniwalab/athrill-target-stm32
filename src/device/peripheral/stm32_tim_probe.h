/*
 * PROVISIONAL, for investigation: the STM32F7 timers PX4 needs to run
 * (TIM8 is PX4 fmu-v5's high-resolution timer), as up-counters with
 * prescaler, auto-reload, four compares and their interrupts. Part of the
 * peripheral probe (stm32_periph_probe.c), which hands their registers here.
 */
#ifndef ATHRILL_STM32_TIM_PROBE_H
#define ATHRILL_STM32_TIM_PROBE_H

#include "std_types.h"
#include "cpu.h"
#include "std_device_ops.h"

/* Whether addr is a modelled timer's register. */
std_bool stm32_tim_probe_owns(uint32 addr);
uint32 stm32_tim_probe_read(uint32 addr);
void stm32_tim_probe_write(uint32 addr, uint32 size, uint32 value);
void stm32_tim_probe_reset(void);
/* The counters up to the device clock, their interrupts, the clocks to the next event. */
void stm32_tim_probe_tick(DeviceClockType *clock);

#endif

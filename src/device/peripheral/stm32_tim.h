/*
 * The register-compatible STM32F7/H7 timers: the register files of the
 * advanced, general-purpose and basic timers, with what
 * software sees of their time base and channels:
 *   - the counter: up, down and the three center-aligned modes, prescaler,
 *     auto-reload, repetition counter, one-pulse mode, the preloads
 *     (ARR, PSC, CCRx) taken at the update event, UDIS, URS, EGR;
 *   - the channels as output compare: CCxIF when the counter matches CCRx
 *     (in center-aligned mode as CMS selects), and the PWM waveform each
 *     output channel drives (stm32_tim_pwm), the point where the devices on
 *     the pins (ESCs, buzzer) attach;
 *   - the status flags and their interrupts in the NVIC.
 * Lightweight on purpose: no input signals (input capture channels never
 * capture), slave modes, trigger outputs and DMA requests are registers
 * only. The counters follow the CPU clock and are brought up to date when
 * read or written and at each device tick, which also reports the clocks to
 * the next enabled interrupt (min_intr_interval).
 */
#ifndef ATHRILL_STM32_TIM_H
#define ATHRILL_STM32_TIM_H

#include "std_types.h"
#include "cpu.h"
#include "std_device_ops.h"

/* Whether addr is in a timer's register block. */
std_bool stm32_tim_owns(uint32 addr);
uint32 stm32_tim_read(uint32 addr);
void stm32_tim_write(uint32 addr, uint32 size, uint32 value);
/* Selects the immutable board descriptor table used by the shared engine. */
void stm32_tim_set_h7(std_bool h7);
void stm32_tim_reset(void);
/* The counters up to the device clock, their interrupts, the clocks to the next interrupt. */
void stm32_tim_tick(DeviceClockType *clock);

/* The waveform an output channel drives on its pin, in CPU clocks. */
typedef struct {
	std_bool enabled;	/* counting, output enabled (CCxE, and MOE on TIM1/TIM8), PWM mode */
	uint64 period_clocks;	/* one PWM period */
	uint64 high_clocks;	/* the pin's high time within a period */
} Stm32TimPwmType;

/* The output of channel (1 to 4) of TIMtimer; FALSE if there is no such channel. */
std_bool stm32_tim_pwm(uint32 timer, uint32 channel, Stm32TimPwmType *pwm);

#endif

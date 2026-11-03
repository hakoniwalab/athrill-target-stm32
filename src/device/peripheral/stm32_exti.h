/*
 * The STM32F7 external interrupt controller EXTI (RM0410 chapter 11) for
 * GPIO lines 0 to 15, with SYSCFG_EXTICR1..4 that choose the port of each
 * line: an edge on a pin sets the pending bit of its line when the edge is
 * selected (RTSR, FTSR), and the line's interrupt is pending while PR and
 * IMR are set. Lines 16 and above (PVD, RTC, USB ...) are registers only.
 */
#ifndef ATHRILL_STM32_EXTI_H
#define ATHRILL_STM32_EXTI_H

#include "std_types.h"

std_bool stm32_exti_owns(uint32 addr);
uint32 stm32_exti_read(uint32 addr);
void stm32_exti_write(uint32 addr, uint32 size, uint32 value);
void stm32_exti_reset(void);

/* Pin of port changed to level (stm32_gpio.c). */
void stm32_exti_pin_edge(uint32 port, uint32 pin, std_bool level);

#endif

/*
 * The STM32F7 GPIO ports GPIOA to GPIOK (RM0410 chapter 6): the register
 * file, ODR with BSRR, and IDR (output pins read their ODR, input pins the
 * level a device drives on them, 0 by default). Lightweight: alternate
 * functions, speed and pull resistors are registers only.
 *
 * Devices on the board see the pins through this module: a chip select
 * (the SPI chip models, stm32_spi.c) and data-ready lines that a chip
 * drives (stm32_gpio_drive).
 */
#ifndef ATHRILL_STM32_GPIO_H
#define ATHRILL_STM32_GPIO_H

#include "std_types.h"

#define STM32_GPIO_PORT(letter)	((uint32)((letter) - 'A'))

std_bool stm32_gpio_owns(uint32 addr);
uint32 stm32_gpio_read(uint32 addr);
void stm32_gpio_write(uint32 addr, uint32 size, uint32 value);
/* Selects the immutable board address table used by the shared engine. */
void stm32_gpio_set_h7(std_bool h7);
void stm32_gpio_reset(void);

/* The level of an output pin (its ODR bit). */
std_bool stm32_gpio_output(uint32 port, uint32 pin);
/* A device drives an input pin to level; a change is an edge for EXTI. */
void stm32_gpio_drive(uint32 port, uint32 pin, std_bool level);

#endif

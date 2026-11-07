/*
 * The GPIO input drive the chip fixtures call for their data-ready pins;
 * test_v6c_sensors defines it (the GPIO model is an athrill-device model).
 * SPDX-License-Identifier: MIT
 */
#ifndef ATHRILL_TEST_FIXTURE_STM32_GPIO_H
#define ATHRILL_TEST_FIXTURE_STM32_GPIO_H

#include "std_types.h"

void stm32_gpio_drive(uint32 port, uint32 pin, std_bool level);

#endif

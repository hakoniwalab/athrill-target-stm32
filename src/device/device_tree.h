/* SPDX-License-Identifier: MIT */
#ifndef ATHRILL_STM32_DEVICE_TREE_H
#define ATHRILL_STM32_DEVICE_TREE_H

#include "std_types.h"
#include "cpu.h"
#include "std_device_ops.h"

/* TRUE after DEVICE_CONFIG_DEVICE_TREE was found and built. */
std_bool stm32_device_tree_build(void);
void stm32_device_tree_tick(DeviceClockType *clock);
void stm32_device_tree_destroy(void);

/* A character of the debugger's console input to the console UART (the
 * athrill-device runtime's console channel 0); FALSE when there is no
 * console UART or its input queue is full. */
std_bool stm32_device_tree_console_input(uint8 data);

#endif

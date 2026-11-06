/*
 * STM32 board profile selected from DEVICE_CONFIG_STM32_BOARD.
 *
 * Selection is needed by the CPU reset as well as by the devices.  In
 * particular, ArmMReset must know the NVIC size before the firmware runs.
 * Missing and unknown board names deliberately retain the STM32F7 profile.
 */
#ifndef ATHRILL_STM32_BOARD_PROFILE_H
#define ATHRILL_STM32_BOARD_PROFILE_H

#include "std_types.h"

typedef enum {
	STM32_BOARD_PROFILE_FMU_V5 = 0,
	STM32_BOARD_PROFILE_FMU_V6C,
} Stm32BoardProfileType;

void stm32_board_profile_select(void);
Stm32BoardProfileType stm32_board_profile(void);
std_bool stm32_board_is_h7(void);
uint32 stm32_board_num_irq(void);

#endif /* ATHRILL_STM32_BOARD_PROFILE_H */

/* The SoC profile (DEVICE_CONFIG_STM32_BOARD): the NVIC interrupt count. */
#include "board/board_profile.h"
#include "device.h"
#include "cpuemu_ops.h"
#include "../test_check.h"
#include <stdio.h>

#include <string.h>

std_bool stm32_device_accessed;
static const char *configured_board;

Std_ReturnType cpuemu_get_devcfg_string(const char *key, char **value)
{
	if ((strcmp(key, "DEVICE_CONFIG_STM32_BOARD") != 0) || (configured_board == NULL)) {
		return STD_E_NOENT;
	}
	*value = (char *)configured_board;
	return STD_E_OK;
}

int main(void)
{
	configured_board = NULL;
	stm32_board_profile_select();
	TEST_CHECK(stm32_board_profile() == STM32_BOARD_PROFILE_FMU_V5);
	TEST_CHECK(stm32_board_num_irq() == 110U);

	configured_board = "fmu-v6c";
	stm32_board_profile_select();
	TEST_CHECK(stm32_board_is_h7());
	TEST_CHECK(stm32_board_num_irq() == 150U);

	configured_board = "unknown";
	stm32_board_profile_select();
	TEST_CHECK(!stm32_board_is_h7());
	TEST_CHECK(stm32_board_num_irq() == 110U);

	puts("board profile tests passed");
	return 0;
}

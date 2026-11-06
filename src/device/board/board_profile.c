#include "board_profile.h"
#include "cpuemu_ops.h"
#include "std_errno.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define STM32F7_NUM_IRQ 110U
#define STM32H7_NUM_IRQ 150U

static Stm32BoardProfileType selected_profile = STM32_BOARD_PROFILE_FMU_V5;

/*
 * The core's string getter writes the value to stdout.  Profile selection is
 * deliberately earlier than the legacy board attachment, so discard that
 * early echo; board_attach() performs the normal, visible lookup later.  This
 * keeps the fmu-v5 console byte-for-byte compatible.
 */
static Std_ReturnType get_board_name_early(char **name)
{
	int saved_stdout;
	int null_fd;
	Std_ReturnType err;

	(void)fflush(stdout);
	saved_stdout = dup(STDOUT_FILENO);
	null_fd = open("/dev/null", O_WRONLY);
	if ((saved_stdout < 0) || (null_fd < 0) || (dup2(null_fd, STDOUT_FILENO) < 0)) {
		if (saved_stdout >= 0) {
			(void)close(saved_stdout);
		}
		if (null_fd >= 0) {
			(void)close(null_fd);
		}
		return cpuemu_get_devcfg_string("DEVICE_CONFIG_STM32_BOARD", name);
	}
	err = cpuemu_get_devcfg_string("DEVICE_CONFIG_STM32_BOARD", name);
	(void)fflush(stdout);
	(void)dup2(saved_stdout, STDOUT_FILENO);
	(void)close(saved_stdout);
	(void)close(null_fd);
	return err;
}

void stm32_board_profile_select(void)
{
	char *name = NULL;

	selected_profile = STM32_BOARD_PROFILE_FMU_V5;
	if ((get_board_name_early(&name) == STD_E_OK)
		&& (strcmp(name, "fmu-v6c") == 0)) {
		selected_profile = STM32_BOARD_PROFILE_FMU_V6C;
	}
}

Stm32BoardProfileType stm32_board_profile(void)
{
	return selected_profile;
}

std_bool stm32_board_is_h7(void)
{
	return selected_profile == STM32_BOARD_PROFILE_FMU_V6C;
}

uint32 stm32_board_num_irq(void)
{
	return stm32_board_is_h7() ? STM32H7_NUM_IRQ : STM32F7_NUM_IRQ;
}

/*
 * Pixhawk 6C board attachment.  Stage 2 attaches only the parameter F-RAM;
 * sensor identities and data-ready timing are the first stage-3 boundary.
 */
#include "fmu_v6c.h"
#include "cpuemu_ops.h"
#include "std_errno.h"
#include "peripheral/stm32_gpio.h"
#include "peripheral/stm32_spi.h"
#include "memory/fm25v02.h"

static Fm25v02Type fram = { .fd = -1 };

void fmu_v6c_attach(void)
{
	char *fram_file = NULL;

	/* Stage 2 enables storage explicitly; an unconfigured run probes an absent chip. */
	if (cpuemu_get_devcfg_string("DEVICE_CONFIG_STM32_FRAM_FILE", &fram_file) != STD_E_OK) {
		return;
	}
	fm25v02_open(&fram, fram_file);
	stm32_spi_attach(2U, STM32_GPIO_PORT('D'), 4U, &fm25v02_ops, &fram);
}

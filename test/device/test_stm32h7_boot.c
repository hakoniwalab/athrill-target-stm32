#include "peripheral/stm32h7_boot.h"
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

static uint32 read32(uint32 addr)
{
	uint32 value = 0U;

	TEST_CHECK(stm32h7_boot_read(addr, 4U, &value) == STD_E_OK);
	return value;
}

static void write32(uint32 addr, uint32 value)
{
	TEST_CHECK(stm32h7_boot_write(addr, 4U, value) == STD_E_OK);
}

int main(void)
{
	uint32 value;

	configured_board = NULL;
	stm32_board_profile_select();
	TEST_CHECK(stm32_board_profile() == STM32_BOARD_PROFILE_FMU_V5);
	TEST_CHECK(stm32_board_num_irq() == 110U);
	TEST_CHECK(stm32h7_boot_read(0x58024400U, 4U, &value) == STD_E_SEGV);

	configured_board = "fmu-v6c";
	stm32_board_profile_select();
	TEST_CHECK(stm32_board_is_h7());
	TEST_CHECK(stm32_board_num_irq() == 150U);
	stm32h7_boot_reset();

	/* RCC, PWR and FLASH are athrill-device register blocks with their own
	 * windows and Devicetree rules (hakoniwalab/athrill-device#13 4-7): the
	 * boot regions do not take their addresses */
	TEST_CHECK(stm32h7_boot_read(0x58024400U, 4U, &value) != STD_E_OK);
	TEST_CHECK(stm32h7_boot_write(0x58024804U, 4U, 0U) != STD_E_OK);
	TEST_CHECK(stm32h7_boot_read(0x52002010U, 4U, &value) != STD_E_OK);
	/* RTC, ADC3, OTG FS, IWDG, RNG are athrill-device register blocks too */
	TEST_CHECK(stm32h7_boot_read(0x5800400CU, 4U, &value) != STD_E_OK);
	TEST_CHECK(stm32h7_boot_read(0x58026000U, 4U, &value) != STD_E_OK);
	TEST_CHECK(stm32h7_boot_read(0x40080010U, 4U, &value) != STD_E_OK);
	TEST_CHECK(stm32h7_boot_write(0x58004800U, 4U, 0U) != STD_E_OK);
	TEST_CHECK(stm32h7_boot_read(0x48021808U, 4U, &value) != STD_E_OK);
	/* SDMMC2 is an athrill-device model with its own window: the boot
	 * regions do not take its addresses (hakoniwalab/athrill-device#13 4-6) */
	TEST_CHECK(stm32h7_boot_read(0x48022434U, 4U, &value) != STD_E_OK);
	TEST_CHECK(stm32h7_boot_write(0x48022438U, 4U, 0x1234U) != STD_E_OK);
	TEST_CHECK((read32(0x5C001000U) & 0xFFFU) == 0x450U);
	write32(0x51008108U, 1U);
	TEST_CHECK(read32(0x51008108U) == 1U);
	write32(0x58025408U, 0xFFU);
	TEST_CHECK(read32(0x58025408U) == 0xFFU);
	write32(0x4000A018U, 0x3U);
	TEST_CHECK(read32(0x4000A018U) == 0x3U);
	write32(0x4000A418U, 0x4U);
	TEST_CHECK(read32(0x4000A418U) == 0x4U);
	write32(0x4000A7FCU, 0x89ABCDEFU);
	TEST_CHECK(read32(0x4000A7FCU) == 0x89ABCDEFU);
	write32(0x4000AC00U, 0x11223344U);
	TEST_CHECK(read32(0x4000AC00U) == 0x11223344U);
	write32(0x4000D3FCU, 0x55667788U);
	TEST_CHECK(read32(0x4000D3FCU) == 0x55667788U);
	TEST_CHECK(stm32h7_boot_read(0x4000A800U, 4U, &value) == STD_E_SEGV);
	TEST_CHECK(stm32h7_boot_read(0x4000D400U, 4U, &value) == STD_E_SEGV);
	TEST_CHECK(stm32h7_boot_read(0x40022400U, 4U, &value) == STD_E_SEGV);
	TEST_CHECK(stm32h7_boot_read(0x48020000U, 4U, &value) == STD_E_SEGV);
	TEST_CHECK(stm32h7_boot_read(0x51000000U, 4U, &value) == STD_E_SEGV);
	TEST_CHECK(stm32h7_boot_read(0x52000000U, 4U, &value) == STD_E_SEGV);
	TEST_CHECK(stm32h7_boot_read(0x58024C00U, 4U, &value) == STD_E_SEGV);
	TEST_CHECK(stm32h7_boot_read(0x40023800U, 4U, &value) == STD_E_SEGV);

	configured_board = "unknown";
	stm32_board_profile_select();
	TEST_CHECK(!stm32_board_is_h7());
	TEST_CHECK(stm32_board_num_irq() == 110U);

	puts("STM32H7 boot-front-end tests passed");
	return 0;
}

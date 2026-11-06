#include "peripheral/stm32h7_boot.h"
#include "board/board_profile.h"
#include "device.h"
#include "cpuemu_ops.h"
#include <stdio.h>

/* The SDMMC2 behavior is tested separately; these stubs check boot-region routing. */
static uint32 sdmmc_write_addr;
static uint32 sdmmc_write_value;

std_bool stm32h7_sdmmc_owns(uint32 addr)
{
	return (addr >= 0x48022400U) && (addr < 0x48022800U);
}

uint32 stm32h7_sdmmc_read(uint32 addr)
{
	(void)addr;
	return 0x44332211U;
}

void stm32h7_sdmmc_write(uint32 addr, uint32 size, uint32 value)
{
	sdmmc_write_addr = addr;
	(void)size;
	sdmmc_write_value = value;
}
#include <string.h>

std_bool stm32_device_accessed;
static const char *configured_board;
static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		printf("FAIL line %d: %s\n", __LINE__, #condition); \
		failures++; \
	} \
} while (0)

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

	CHECK(stm32h7_boot_read(addr, 4U, &value) == STD_E_OK);
	return value;
}

static void write32(uint32 addr, uint32 value)
{
	CHECK(stm32h7_boot_write(addr, 4U, value) == STD_E_OK);
}

int main(void)
{
	uint32 value;

	configured_board = NULL;
	stm32_board_profile_select();
	CHECK(stm32_board_profile() == STM32_BOARD_PROFILE_FMU_V5);
	CHECK(stm32_board_num_irq() == 110U);
	CHECK(stm32h7_boot_read(0x58024400U, 4U, &value) == STD_E_SEGV);

	configured_board = "fmu-v6c";
	stm32_board_profile_select();
	CHECK(stm32_board_is_h7());
	CHECK(stm32_board_num_irq() == 150U);
	stm32h7_boot_reset();

	write32(0x58024400U, 1U);
	CHECK((read32(0x58024400U) & (1U << 2)) != 0U);
	write32(0x58024400U, (1U << 12) | (1U << 16) | (1U << 24) | (1U << 26) | (1U << 28));
	value = read32(0x58024400U);
	CHECK((value & ((1U << 13) | (1U << 17) | (1U << 25) | (1U << 27) | (1U << 29)))
		== ((1U << 13) | (1U << 17) | (1U << 25) | (1U << 27) | (1U << 29)));

	write32(0x58024410U, 3U);
	CHECK((read32(0x58024410U) & (7U << 3)) == (3U << 3));
	CHECK((read32(0x58024804U) & (1U << 13)) != 0U);
	CHECK((read32(0x58024818U) & (1U << 13)) != 0U);
	write32(0x5802480CU, 1U << 24);
	CHECK((read32(0x5802480CU) & (1U << 26)) != 0U);

	write32(0x52002000U, 0x22U);
	CHECK(read32(0x52002000U) == 0x22U);
	write32(0x5800400CU, 1U << 7);
	CHECK((read32(0x5800400CU) & ((1U << 5) | (1U << 6)))
		== ((1U << 5) | (1U << 6)));

	write32(0x58026008U, (1U << 31) | 1U);
	CHECK((read32(0x58026008U) & (1U << 31)) == 0U);
	CHECK((read32(0x58026000U) & 1U) != 0U);
	CHECK(read32(0x58026040U) == 0U);
	CHECK(read32(0x48022434U) == 0x44332211U);
	write32(0x48022438U, 0x1234U);
	CHECK(sdmmc_write_addr == 0x48022438U);
	CHECK(sdmmc_write_value == 0x1234U);
	write32(0x58001C04U, 1U << 13);
	CHECK((read32(0x58001C18U) & ((1U << 4) | (1U << 5))) == ((1U << 4) | (1U << 5)));

	CHECK((read32(0x5C001000U) & 0xFFFU) == 0x450U);
	CHECK(stm32h7_boot_read(0x40023800U, 4U, &value) == STD_E_SEGV);

	configured_board = "unknown";
	stm32_board_profile_select();
	CHECK(!stm32_board_is_h7());
	CHECK(stm32_board_num_irq() == 110U);

	if (failures == 0) {
		puts("STM32H7 boot-front-end tests passed");
	}
	return failures != 0;
}

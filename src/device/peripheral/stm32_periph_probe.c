/*
 * PROVISIONAL, for investigation (see stm32_periph_probe.h): every register
 * keeps what is written to it, with the ready bits the start-up code waits
 * for (RCC oscillators and PLLs, the system clock switch, PWR over-drive,
 * the regulator, the RTC's synchronisation and initialisation, the USB OTG
 * core's resets, an SD card slot without a card, SPI buses with nothing on
 * them, an ADC whose conversions are always done); the timers PX4 needs are
 * modelled in athrill-device from the Devicetree,, and so are the USART/UARTs. The first access to each 1 KiB block of
 * registers is reported on stderr.
 *
 * DEVICE_CONFIG_STM32_PROBE_TRACE 1 also reports, on stderr, every access
 * that still reaches this probe: one line per 1 KiB block, direction and
 * owner (the target model behind the address, or the register file), with
 * the PC of the first such access. A model moved to athrill-device must no
 * longer appear (hakoniwalab/athrill-device#13 step 4).
 */
#include "stm32_periph_probe.h"
#include "device.h"
#include "cpu.h"
#include "cpuemu_ops.h"
#include "concrete_executor/target/dbg_target_serial.h"
#include "stm32h7_boot.h"
#include "board/board_profile.h"
#include <stdio.h>
#include "std_device_ops.h"

uint8 stm32_probe_apb_ahb1_data[STM32_PROBE_APB_AHB1_SIZE];
uint8 stm32_probe_ahb2_data[STM32_PROBE_AHB2_SIZE];
uint8 stm32_probe_sysmem_data[STM32_PROBE_SYSMEM_SIZE];
uint8 stm32_probe_dbgmcu_data[STM32_PROBE_DBGMCU_SIZE];

#define PROBE_TRACE_MAX	4096U

static std_bool trace_enabled;
static uint32 traced[PROBE_TRACE_MAX];
static uint32 traced_count;

void stm32_probe_init(void)
{
	static const uint8 uid[12] = { 0x41, 0x54, 0x48, 0x52, 0x49, 0x4C, 0x4C, 0x20, 0x53, 0x54, 0x4D, 0x37 };
	uint32 i;
	uint32 trace = 0U;

	(void)cpuemu_get_devcfg_value("DEVICE_CONFIG_STM32_PROBE_TRACE", &trace);
	trace_enabled = (trace != 0U) ? TRUE : FALSE;
	traced_count = 0U;

	if (stm32_board_is_h7()) {
		stm32h7_boot_reset();
		return;
	}
	for (i = 0U; i < sizeof(uid); i++) {
		stm32_probe_sysmem_data[0xF420U + i] = uid[i];
	}
	stm32_probe_sysmem_data[0xF442U] = 0x00U;	/* 2048 KiB of flash */
	stm32_probe_sysmem_data[0xF443U] = 0x08U;
	/* DBGMCU_IDCODE: REV_ID 0x1001 (revision Z), DEV_ID 0x451 (STM32F76x/77x) */
	stm32_probe_dbgmcu_data[0] = 0x51U;
	stm32_probe_dbgmcu_data[1] = 0x64U;
	stm32_probe_dbgmcu_data[2] = 0x01U;
	stm32_probe_dbgmcu_data[3] = 0x10U;
}


static uint8 reported[(STM32_PROBE_APB_AHB1_SIZE + STM32_PROBE_AHB2_SIZE) / 1024U];

static void report(uint32 addr, const char *what)
{
	uint32 index;

	if ((addr < STM32_PROBE_APB_AHB1_BASE) || (addr >= 0xE0000000U)) {
		return;
	}
	index = (addr >= STM32_PROBE_AHB2_BASE)
		? ((STM32_PROBE_APB_AHB1_SIZE + (addr - STM32_PROBE_AHB2_BASE)) / 1024U) : ((addr - STM32_PROBE_APB_AHB1_BASE) / 1024U);

	if (reported[index] == 0U) {
		reported[index] = 1U;
		fprintf(stderr, "PROBE: first %s at 0x%08x\n", what, addr);
	}
}

/* The target model behind an address (the dispatch below), or the probe's
 * own storage. */
static const char *trace_owner(uint32 addr)
{
	if (stm32_board_is_h7()) {
		return "h7boot";
	}
	return "register-file";
}

static void trace_access(uint32 addr, std_bool write)
{
	uint32 key;
	uint32 i;

	if (!trace_enabled) {
		return;
	}
	key = (addr & ~0x3FFU) | (write ? 1U : 0U);
	for (i = 0U; i < traced_count; i++) {
		if (traced[i] == key) {
			return;
		}
	}
	if (traced_count < PROBE_TRACE_MAX) {
		traced[traced_count++] = key;
	}
	fprintf(stderr, "PROBE_TRACE %s %s 0x%08x pc=0x%08x\n", write ? "W" : "R",
		trace_owner(addr), addr & ~0x3FFU, (uint32)virtual_cpu.cores[0].core.reg.pc);
}

static uint32 *word_at(MpuAddressRegionType *r, uint32 addr)
{
	return (uint32 *)&r->data[(addr & ~3U) - (uint32)r->start];
}

/* The value read: what was written (the read rules are in athrill-device's
 * register blocks now). */
static uint32 read_word(MpuAddressRegionType *r, uint32 addr)
{
	return *word_at(r, addr & ~3U);
}

static void write_bytes(MpuAddressRegionType *r, uint32 addr, uint32 size, uint32 value)
{
	uint32 i;

	for (i = 0U; i < size; i++) {
		r->data[addr - (uint32)r->start + i] = (uint8)(value >> (8U * i));
	}
}

static Std_ReturnType get8(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 *d)
{
	uint32 value;
	(void)c;
	trace_access((uint32)a, FALSE);
	if (stm32_board_is_h7()) {
		return stm32h7_boot_read((uint32)a, 1U, &value) == STD_E_OK
			? (*d = (uint8)value, STD_E_OK) : STD_E_SEGV;
	}
	report((uint32)a, "read");
	*d = (uint8)(read_word(r, (uint32)a) >> (8U * (a & 3U)));
	return STD_E_OK;
}

static Std_ReturnType get16(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint16 *d)
{
	uint32 value;
	(void)c;
	trace_access((uint32)a, FALSE);
	if (stm32_board_is_h7()) {
		return stm32h7_boot_read((uint32)a, 2U, &value) == STD_E_OK
			? (*d = (uint16)value, STD_E_OK) : STD_E_SEGV;
	}
	report((uint32)a, "read");
	*d = (uint16)(read_word(r, (uint32)a) >> (8U * (a & 2U)));
	return STD_E_OK;
}

static Std_ReturnType get32(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint32 *d)
{
	(void)c;
	trace_access((uint32)a, FALSE);
	if (stm32_board_is_h7()) {
		return stm32h7_boot_read((uint32)a, 4U, d);
	}
	report((uint32)a, "read");
	*d = read_word(r, (uint32)a);
	return STD_E_OK;
}

static Std_ReturnType put8(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 d)
{
	(void)c;
	trace_access((uint32)a, TRUE);
	stm32_device_accessed = TRUE;
	if (stm32_board_is_h7()) {
		return stm32h7_boot_write((uint32)a, 1U, d);
	}
	report((uint32)a, "write");
	write_bytes(r, (uint32)a, 1U, d);
	return STD_E_OK;
}

static Std_ReturnType put16(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint16 d)
{
	(void)c;
	trace_access((uint32)a, TRUE);
	stm32_device_accessed = TRUE;
	if (stm32_board_is_h7()) {
		return stm32h7_boot_write((uint32)a, 2U, d);
	}
	report((uint32)a, "write");
	write_bytes(r, (uint32)a, 2U, d);
	return STD_E_OK;
}

static Std_ReturnType put32(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint32 d)
{
	(void)c;
	trace_access((uint32)a, TRUE);
	stm32_device_accessed = TRUE;
	if (stm32_board_is_h7()) {
		return stm32h7_boot_write((uint32)a, 4U, d);
	}
	report((uint32)a, "write");
	write_bytes(r, (uint32)a, 4U, d);
	return STD_E_OK;
}

static Std_ReturnType get_pointer(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 **d)
{
	(void)c;
	if (stm32_board_is_h7()) {
		return STD_E_INVALID;
	}
	*d = &r->data[a - r->start];
	return STD_E_OK;
}

MpuAddressRegionOperationType stm32_probe_ops = { get8, get16, get32, put8, put16, put32, get_pointer };

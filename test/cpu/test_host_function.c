#include "athrill_host_function_internal.h"
#include "cpu.h"
#include "cpuemu_ops.h"
#include "mpu_ops.h"
#include "symbol_ops.h"

#include <stdio.h>
#include <string.h>

#ifndef TEST_HOST_LIBRARY
#error TEST_HOST_LIBRARY is required
#endif

#define RAM_BASE 0x20000000U
#define DEVICE_BASE 0x40000000U

CpuType virtual_cpu;
static uint8 ram[256];
static uint32 invalidations;

Std_ReturnType cpuemu_get_devcfg_string(const char *key, char **value)
{
	if (strcmp(key, "DEVICE_CONFIG_HOST_FUNCTION_LIBRARY") != 0) {
		return STD_E_NOENT;
	}
	*value = (char *)TEST_HOST_LIBRARY;
	return STD_E_OK;
}

Std_ReturnType cpuemu_get_devcfg_string_pair_at(const char *key, uint32 occurrence,
	char **first, char **second)
{
	static char *guest[] = { "sum_guest", "device_guest" };
	static char *host[] = { "test_sum", "test_device_write" };

	if ((strcmp(key, "DEVICE_CONFIG_HOST_FUNCTION") != 0) || (occurrence >= 2U)) {
		return STD_E_NOENT;
	}
	*first = guest[occurrence];
	*second = host[occurrence];
	return STD_E_OK;
}

int symbol_get_func(char *name, uint32 length, AthrillAddrType *address, uint32 *size)
{
	(void)length;
	*size = 2U;
	if (strcmp(name, "sum_guest") == 0) {
		*address = 0x1001U; /* ELF Thumb symbol: canonical entry is 0x1000. */
		return 0;
	}
	if (strcmp(name, "device_guest") == 0) {
		*address = 0x2000U;
		return 1;
	}
	return -1;
}

Std_ReturnType mpu_get_host_memory(CoreIdType core_id, AthrillAddrType address,
	AthrillAddrType size, std_bool write, uint8 **data)
{
	(void)core_id;
	(void)write;
	if ((address < RAM_BASE) || (address + size < address)
		|| (address + size > RAM_BASE + sizeof(ram))) {
		return STD_E_SEGV;
	}
	*data = &ram[address - RAM_BASE];
	return STD_E_OK;
}

void thumb_invalidate_code(AthrillAddrType address, uint32 size)
{
	(void)address;
	(void)size;
	invalidations++;
}

static int fail(const char *message)
{
	fprintf(stderr, "FAIL: %s\n", message);
	return 1;
}

int main(void)
{
	TargetCoreType *core = &virtual_cpu.cores[0].core;
	uint32 stack_argument = 32U;
	int sum;
	int device;

	virtual_cpu.core_id_num = 1U;
	core->core_id = 0U;
	stm32_host_function_init();
	if (athrill_host_function_init() != STD_E_OK) {
		return fail("initialisation");
	}
	sum = athrill_host_function_find(0x1000U);
	device = athrill_host_function_find(0x2001U);
	if ((sum < 0) || (device < 0)) {
		return fail("entry lookup and Thumb canonicalisation");
	}

	memcpy(ram, &stack_argument, sizeof(stack_argument));
	core->reg.r[0] = 10U;
	core->reg.r[CPU_REG_SP] = RAM_BASE;
	core->reg.r[CPU_REG_LR] = 0x1235U;
	core->reg.next_pc = 0x1002U;
	if (!athrill_host_function_invoke(0U, sum)) {
		return fail("handled sum call");
	}
	if ((core->reg.r[0] != 42U) || (core->reg.next_pc != 0x1234U)) {
		return fail("AAPCS arguments, result, or LR return");
	}

	core->reg.r[0] = 77U;
	core->reg.next_pc = 0x2002U;
	if (athrill_host_function_invoke(0U, device)) {
		return fail("device write must refuse");
	}
	if ((core->reg.r[0] != 77U) || (core->reg.next_pc != 0x2002U)
		|| (invalidations != 0U)) {
		return fail("refused call changed guest state");
	}
	puts("host function core and Arm AAPCS: PASS");
	return 0;
}

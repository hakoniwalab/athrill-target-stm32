#include "athrill_host_function_internal.h"
#include "cpu.h"
#include "target_cpu.h"
#include <stdint.h>

static Std_ReturnType arm_argument(CoreIdType core_id, uint32 index, uint64 *value)
{
	TargetCoreType *core = &virtual_cpu.cores[core_id].core;
	uint32 stack_value;
	uint32 address;

	if (index < 4U) {
		*value = core->reg.r[index];
		return STD_E_OK;
	}
	if ((index - 4U) > ((UINT32_MAX - core->reg.r[CPU_REG_SP]) / 4U)) {
		return STD_E_SEGV;
	}
	address = core->reg.r[CPU_REG_SP] + ((index - 4U) * 4U);
	if (athrill_host_function_memory_read(core_id, address,
		&stack_value, sizeof(stack_value)) != STD_E_OK) {
		return STD_E_SEGV;
	}
	*value = stack_value;
	return STD_E_OK;
}

static void arm_set_result(CoreIdType core_id, uint64 value)
{
	virtual_cpu.cores[core_id].core.reg.r[0] = (uint32)value;
}

static void arm_return_from_call(CoreIdType core_id)
{
	TargetCoreType *core = &virtual_cpu.cores[core_id].core;

	/* next_pc becomes PC when the current (host) instruction completes. */
	core->reg.next_pc = core->reg.r[CPU_REG_LR] & ~1U;
}

static AthrillAddrType arm_canonical_address(AthrillAddrType address)
{
	return address & ~(AthrillAddrType)1U;
}

static const AthrillHostFunctionTargetOps arm_host_function_ops = {
	.name = "Arm AAPCS",
	.arg = arm_argument,
	.set_result = arm_set_result,
	.return_from_call = arm_return_from_call,
	.canonical_address = arm_canonical_address,
	.invalidate_code = thumb_invalidate_code,
};

void stm32_host_function_init(void)
{
	athrill_host_function_register_target(&arm_host_function_ops);
}

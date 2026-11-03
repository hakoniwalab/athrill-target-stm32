#include "cpu.h"
#include "bus.h"
#include "std_cpu_ops.h"
#include "mpu_types.h"
#include <stdio.h>
#include <string.h>

CpuType virtual_cpu;

static void reset_core(TargetCoreType *core)
{
	CoreIdType id = core->core_id;

	memset(core, 0, sizeof(*core));
	core->core_id = id;
	core->reset_pending = TRUE;
}

/*
 * Reset as Armv7-M does it: the main stack pointer and the reset handler's
 * address are read from the first two words of the vector table. This is done
 * at the first clock, once the program has been loaded.
 */
static Std_ReturnType take_reset(TargetCoreType *core)
{
	uint32 sp;
	uint32 entry;

	if ((bus_get_data32(core->core_id, STM32_RESET_VTOR, &sp) != STD_E_OK)
		|| (bus_get_data32(core->core_id, STM32_RESET_VTOR + 4U, &entry) != STD_E_OK)) {
		printf("ERROR: no vector table at 0x%08x\n", STM32_RESET_VTOR);
		return STD_E_SEGV;
	}
	core->reg.r[CPU_REG_SP] = sp & ~3U;
	core->reg.pc = entry & ~1U;
	core->reg.r[CPU_REG_LR] = 0xFFFFFFFFU;
	core->reset_pending = FALSE;
	return STD_E_OK;
}

void cpu_init(void)
{
	CoreIdType i;

	for (i = 0; i < cpu_config_get_core_id_num(); i++) {
		virtual_cpu.cores[i].core.core_id = i;
		cpu_reset(i);
	}
}

void cpu_reset(CoreIdType id)
{
	reset_core(&virtual_cpu.cores[id].core);
}

void cpu_set_current_core(CoreIdType id)
{
	virtual_cpu.current_core = &virtual_cpu.cores[id];
}

std_bool cpu_is_halt(CoreIdType id)
{
	return virtual_cpu.cores[id].core.is_halt;
}

std_bool cpu_is_halt_all(void)
{
	return cpu_is_halt(0);
}

void cpu_mpu_construct_containers(CoreIdType id)
{
	(void)id;
}

Std_ReturnType cpu_supply_clock(CoreIdType id)
{
	TargetCoreType *core = &virtual_cpu.cores[id].core;
	Std_ReturnType err;

	if (core->reset_pending) {
		err = take_reset(core);
		if (err != STD_E_OK) {
			return err;
		}
	}
	core->internal_cycle++;
	virtual_cpu.cores[id].elaps++;
	if (core->is_halt) {
		return STD_E_OK;
	}
	err = thumb_execute(core);
	if (err == STD_E_INVALID) {
		uint16 insn = 0U;

		(void)bus_get_data16(core->core_id, core->reg.pc, &insn);
		printf("ERROR: unsupported instruction 0x%04x at 0x%08x\n", insn, core->reg.pc);
	}
	return err;
}

void cpu_illegal_opcode_trap(CoreIdType id)
{
	(void)id;
}

std_bool cpu_illegal_access(CoreIdType id)
{
	std_bool v = virtual_cpu.cores[id].core.access_fault;

	virtual_cpu.cores[id].core.access_fault = FALSE;
	return v;
}

std_bool cpu_has_permission(CoreIdType id, MpuAddressRegionEnumType region,
                            CpuMemoryAccessType access, AthrillAddrType addr, AthrillAddrType size)
{
	(void)id;
	(void)region;
	(void)access;
	(void)addr;
	(void)size;
	return TRUE;
}

uint32 cpu_get_current_core_id(void)
{
	return virtual_cpu.current_core ? virtual_cpu.current_core->core.core_id : 0;
}

uint32 cpu_get_ep(const TargetCoreType *core)
{
	(void)core;
	return 0;
}

uint32 cpu_get_current_core_register(uint32 i)
{
	return virtual_cpu.current_core->core.reg.r[i & 0xFU];
}

AthrillAddrType cpu_get_current_core_pc(void)
{
	return virtual_cpu.current_core->core.reg.pc;
}

void cpu_set_core_pc(CoreIdType id, AthrillAddrType pc)
{
	virtual_cpu.cores[id].core.reg.pc = (uint32)pc;
}

uint32 cpu_get_current_core_sp(void)
{
	return virtual_cpu.current_core->core.reg.r[CPU_REG_SP];
}

uint32 cpu_get_current_core_ep(void)
{
	return 0;
}

AthrillAddrType cpu_get_return_addr(const TargetCoreType *core)
{
	return core->reg.r[CPU_REG_LR];
}

CoreIdType cpu_get_core_id(const TargetCoreType *core)
{
	return core->core_id;
}

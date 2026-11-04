#include "cpu.h"
#include "bus.h"
#include "std_cpu_ops.h"
#include "mpu_types.h"
#include "cpuemu_ops.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "arm_common/arm_m_exception.h"
#include "symbol_ops.h"
#include "thumb_fast.h"
#include "device.h"

/* STM32F76x: 110 interrupt channels, 4 priority bits, FPv5-D16; Cortex-M7 r1p0. */
#define STM32_NUM_IRQ		110U
#define STM32_PRIORITY_BITS	4U
#define STM32_CPUID		0x411FC270U
#define STM32_CCR_WRITABLE	0x0007031BU

CpuType virtual_cpu;

/*
 * Rare paths called from cpu_supply_clock(), the hot path: kept out of line,
 * or their stack frames (symbol lookup buffers) would be set up at every
 * instruction.
 */
#if defined(__GNUC__) || defined(__clang__)
#define COLD_PATH	__attribute__((noinline, cold))
#else
#define COLD_PATH
#endif

COLD_PATH static void reset_core(TargetCoreType *core)
{
	CoreIdType id = core->core_id;

	memset(core, 0, sizeof(*core));
	core->core_id = id;
	core->reset_pending = TRUE;
	ArmMReset(&core->m, STM32_NUM_IRQ, STM32_PRIORITY_BITS, TRUE);
	core->scs.cpuid = STM32_CPUID;
	core->scs.ccr_writable = STM32_CCR_WRITABLE;
}

/*
 * Reset as Armv7-M does it: the main stack pointer and the reset handler's
 * address are read from the first two words of the vector table. This is done
 * at the first clock, once the program has been loaded.
 */
/*
 * The idle function of the firmware's OS (DEVICE_CONFIG_IDLE_FUNCTION,
 * "up_idle" for NuttX by default; "none" for none): when the core reaches it
 * with no exception to take, it sleeps as at WFI. An idle loop that does not
 * sleep (NuttX's up_idle on PX4 is empty) then lets the halted CPU skip to
 * the next event instead of spinning until it.
 */
static uint32 idle_function(void)
{
	char *name = "up_idle";
	AthrillAddrType addr;
	uint32 size;

	(void)cpuemu_get_devcfg_string("DEVICE_CONFIG_IDLE_FUNCTION", &name);
	if (symbol_get_func(name, (uint32)strlen(name), &addr, &size) < 0) {
		return 0U;
	}
	return (uint32)addr & ~1U;
}

COLD_PATH static Std_ReturnType take_reset(TargetCoreType *core)
{
	uint32 sp;
	uint32 entry;
	uint32 boot = STM32_RESET_VTOR;

	/* DEVICE_CONFIG_STM32_BOOT_ADDR: the vector table to boot from (BOOT_ADD0),
	 * e.g. an application after a bootloader */
	if (cpuemu_get_devcfg_value_hex("DEVICE_CONFIG_STM32_BOOT_ADDR", &boot) != STD_E_OK) {
		(void)cpuemu_get_devcfg_value("DEVICE_CONFIG_STM32_BOOT_ADDR", &boot);
	}
	if ((bus_get_data32(core->core_id, boot, &sp) != STD_E_OK)
		|| (bus_get_data32(core->core_id, boot + 4U, &entry) != STD_E_OK)) {
		printf("ERROR: no vector table at 0x%08x\n", boot);
		return STD_E_SEGV;
	}
	core->m.vtor = boot;	/* the boot address: the flash, aliased at 0 on the device */
	core->idle_pc = idle_function();
	if (core->idle_pc != 0U) {
		fprintf(stderr, "INFO: idle function at 0x%08x\n", core->idle_pc);
	}
	core->reg.r[CPU_REG_SP] = sp & ~3U;
	core->reg.pc = entry & ~1U;
	core->reg.r[CPU_REG_LR] = 0xFFFFFFFFU;
	core->reset_pending = FALSE;
	return STD_E_OK;
}

/* PROVISIONAL, for investigation: the instructions executed, and with
 * DEBUG_PC_SAMPLE_START (the instruction count to start at) a PC sample every
 * PC_SAMPLE_INTERVAL instructions (in 64-byte blocks), printed when the run ends. */
#define PC_SAMPLE_INTERVAL	1000U
#define PC_SAMPLE_MAX		4096U
static uint32 pc_sample_block[PC_SAMPLE_MAX];
static uint32 pc_sample_count[PC_SAMPLE_MAX];
static uint32 pc_sample_used;
static uint64 sampled_clocks;
static uint64 halted_samples;
static uint32 sample_countdown = PC_SAMPLE_INTERVAL;

static void record_sample(const TargetCoreType *core);
static std_bool pc_sampling;		/* DEBUG_PC_SAMPLE_START given */
static uint32 pc_sample_start;
static uint64 run_steps;		/* instructions of thumb_execute_run, without sampling */

static inline void sample_pc(const TargetCoreType *core)
{
	sampled_clocks++;
	if (!pc_sampling || (--sample_countdown != 0U)) {
		return;
	}
	sample_countdown = PC_SAMPLE_INTERVAL;
	record_sample(core);
}

COLD_PATH static void record_sample(const TargetCoreType *core)
{
	uint32 block = core->reg.pc >> 6;
	uint32 i;

	if (sampled_clocks < pc_sample_start) {
		return;
	}
	if (core->is_halt) {
		halted_samples++;
		return;
	}
	for (i = 0U; i < pc_sample_used; i++) {
		if (pc_sample_block[i] == block) {
			pc_sample_count[i]++;
			return;
		}
	}
	if (pc_sample_used < PC_SAMPLE_MAX) {
		pc_sample_block[pc_sample_used] = block;
		pc_sample_count[pc_sample_used] = 1U;
		pc_sample_used++;
	}
}

static void print_final_state(void)
{
	const TargetCoreType *core = &virtual_cpu.cores[0].core;
	uint32 i;

	fprintf(stderr, "SAMPLES halted=%llu steps=%llu\n", (unsigned long long)halted_samples,
		(unsigned long long)(sampled_clocks + run_steps));
	for (i = 0U; i < pc_sample_used; i++) {
		fprintf(stderr, "SAMPLE 0x%08x %u\n", pc_sample_block[i] << 6, pc_sample_count[i]);
	}

	fprintf(stderr, "FINAL pc=0x%08x lr=0x%08x sp=0x%08x ipsr=%u halt=%d primask=%u basepri=0x%x\n",
		core->reg.pc, core->reg.r[14], core->reg.r[13], core->m.ipsr, (int)core->is_halt, core->m.primask, core->m.basepri);
}

std_bool stm32_fp_host_fast = TRUE;
std_bool stm32_exec_fast = TRUE;

/* A mode of the device configuration: fast (default) or strict (the Arm pseudo-code only). */
static std_bool read_mode(const char *key)
{
	char *mode = NULL;

	if (cpuemu_get_devcfg_string(key, &mode) != STD_E_OK) {
		return TRUE;
	}
	if (strcmp(mode, "strict") == 0) {
		return FALSE;
	}
	if (strcmp(mode, "fast") != 0) {
		printf("WARNING: %s %s: fast or strict; fast is used\n", key, mode);
	}
	return TRUE;
}

/* DEVICE_CONFIG_ARM_FP_MODE (floating-point arithmetic), DEVICE_CONFIG_ARM_EXEC_MODE (thumb_fast.h). */
static void read_fp_mode(void)
{
	stm32_fp_host_fast = read_mode("DEVICE_CONFIG_ARM_FP_MODE");
	stm32_exec_fast = read_mode("DEVICE_CONFIG_ARM_EXEC_MODE");
}

void cpu_init(void)
{
	CoreIdType i;

	read_fp_mode();
	pc_sampling = (cpuemu_get_devcfg_value("DEBUG_PC_SAMPLE_START", &pc_sample_start) == STD_E_OK);
	atexit(print_final_state);
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

/* The instruction the run stops at. */
COLD_PATH static void report_stop(const TargetCoreType *core, Std_ReturnType err)
{
	uint32 insn;
	uint32 length;
	const char *name = thumb_instruction_at(core, core->reg.pc, &insn, &length);
	const char *what = "unsupported instruction";

	if (err == STD_E_INVALID) {
		what = "UNPREDICTABLE instruction";
	}
	else if (err == STD_E_EXEC) {
		what = "lockup (a fault that cannot be taken) at instruction";
	}
	printf("ERROR: %s 0x%0*x (%s) at 0x%08x\n", what, (int)(length * 2U), insn, name, core->reg.pc);
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
	sample_pc(core);
	virtual_cpu.cores[id].elaps++;
	if (core->scs.reset_requested) {
		/* AIRCR.SYSRESETREQ */
		reset_core(core);
		return STD_E_OK;
	}
	if (core->is_halt) {
		/* WFI, WFE: woken by an exception that would preempt were PRIMASK clear, or by an event (WFE) */
		uint32 primask = core->m.primask;

		core->m.primask = 0U;
		if ((ArmMPendingToTake(&core->m) != 0U) || (core->wfe && core->m.event)) {
			core->is_halt = FALSE;
			if (core->wfe) {
				core->m.event = FALSE;
			}
		}
		core->m.primask = primask;
		if (core->is_halt) {
			return STD_E_OK;
		}
	}
	if ((core->idle_pc != 0U) && (core->reg.pc == core->idle_pc) && (ArmMPendingToTakeFast(&core->m) == 0U)) {
		core->is_halt = TRUE;
		core->wfe = FALSE;
		return STD_E_OK;
	}
	err = thumb_execute(core);
	if ((err == STD_E_DECODE) || (err == STD_E_INVALID) || (err == STD_E_EXEC)) {
		report_stop(core, err);
	}
	return err;
}

/*
 * Up to max_clocks instructions without the devices in between: stops when
 * the core halts or touches a device register (the devices then tick and
 * recompute their next event). The core's budget never passes a device
 * interrupt (min_intr_interval), and devices read cpuemu_get_clock() on access.
 */
Std_ReturnType cpu_supply_clocks(CoreIdType core_id, uint64 max_clocks, uint64 *clock, uint64 *executed)
{
	TargetCoreType *core = &virtual_cpu.cores[core_id].core;
	Std_ReturnType err = STD_E_OK;
	uint64 n = 0U;

	stm32_device_accessed = FALSE;
	while ((n < max_clocks) && !core->is_halt && !stm32_device_accessed) {
		std_bool special = core->reset_pending || core->scs.reset_requested || (core->reg.pc == core->idle_pc);
		uint64 k;

		if (!special && !pc_sampling) {
			/* the common case: instructions in a row, their counts added after */
			err = thumb_execute_run(core, max_clocks - n, clock, &k);
			n += k;
			core->internal_cycle += k;
			virtual_cpu.cores[core_id].elaps += k;
			run_steps += k;
			if (err != STD_E_OK) {
				if ((err == STD_E_DECODE) || (err == STD_E_INVALID) || (err == STD_E_EXEC)) {
					report_stop(core, err);
				}
				break;
			}
			continue;
		}
		(*clock)++;
		n++;
		if (special) {
			err = cpu_supply_clock(core_id);
		}
		else {
			/* cpu_supply_clock() for a running core with nothing else to do */
			core->internal_cycle++;
			sample_pc(core);
			virtual_cpu.cores[core_id].elaps++;
			err = thumb_execute(core);
			if ((err == STD_E_DECODE) || (err == STD_E_INVALID) || (err == STD_E_EXEC)) {
				report_stop(core, err);
			}
		}
		if (err != STD_E_OK) {
			break;
		}
	}
	*executed = n;
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

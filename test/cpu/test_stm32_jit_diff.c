/* Differential test of the STM32 Thumb JIT against thumb_execute_run(). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "target_cpu.h"
#include "memory_window.h"
#include "arm_common/arm_m_exception.h"
#include "std_errno.h"

#define ROM_BASE 0x08000000U
#define ROM_SIZE 0x4000U
#define CODE_BASE (ROM_BASE + 0x100U)
#define RAM_BASE 0x20000000U
#define RAM_SIZE 0x1000U
#define DEVICE_ADDR 0x40000000U

MemoryWindowType memory_windows[2];
MemoryWindowType memory_code_window;
std_bool stm32_exec_fast = TRUE;
std_bool stm32_fp_host_fast = TRUE;
std_bool stm32_device_accessed;
uint32 cpu_pc_trace_skip_remaining;
uint32 cpu_pc_trace_remaining;

static uint8 rom[ROM_SIZE];
static uint8 ram_a[RAM_SIZE];
static uint8 ram_b[RAM_SIZE];
static uint8 ram_seed[RAM_SIZE];
static uint8 *active_ram;
static uint32 device_value;
static uint32 device_writes;
static int failures;
static uint64 checks;
static TargetCoreType last_interpreter;
static TargetCoreType last_jit;

static std_bool holds(uint32 base, uint32 size, uint32 address, uint32 width)
{
	return ((address - base) < size) && ((size - (address - base)) >= width);
}

uint8 *memory_window_lookup(uint32 address, uint32 size)
{
	MemoryWindowType found;

	if (holds(RAM_BASE, RAM_SIZE, address, size)) {
		found.start = RAM_BASE;
		found.size = RAM_SIZE;
		found.data = active_ram;
		found.writable = TRUE;
	} else if (holds(ROM_BASE, ROM_SIZE, address, size)) {
		found.start = ROM_BASE;
		found.size = ROM_SIZE;
		found.data = rom;
		found.writable = FALSE;
	} else {
		return NULL;
	}
	memory_windows[1] = memory_windows[0];
	memory_windows[0] = found;
	return &found.data[address - found.start];
}

std_bool memory_window_region(uint32 address, MemoryWindowType *region)
{
	if (holds(RAM_BASE, RAM_SIZE, address, 1U)) {
		region->start = RAM_BASE;
		region->size = RAM_SIZE;
		region->data = active_ram;
		region->writable = TRUE;
		return TRUE;
	}
	if (holds(ROM_BASE, ROM_SIZE, address, 1U)) {
		region->start = ROM_BASE;
		region->size = ROM_SIZE;
		region->data = rom;
		region->writable = FALSE;
		return TRUE;
	}
	return FALSE;
}

uint8 *memory_window_code_lookup(uint32 address, uint32 size)
{
	if (!holds(ROM_BASE, ROM_SIZE, address, size)) {
		return NULL;
	}
	memory_code_window.start = ROM_BASE;
	memory_code_window.size = ROM_SIZE;
	memory_code_window.data = rom;
	memory_code_window.writable = FALSE;
	return &rom[address - ROM_BASE];
}

static Std_ReturnType memory_read(AthrillAddrType address, void *data, uint32 size)
{
	uint8 *p = memory_window_lookup((uint32)address, size);

	if (p == NULL) {
		if ((address == DEVICE_ADDR) && (size == 4U)) {
			memcpy(data, &device_value, 4U);
			return STD_E_OK;
		}
		return STD_E_SEGV;
	}
	memcpy(data, p, size);
	return STD_E_OK;
}

static Std_ReturnType memory_write(AthrillAddrType address, const void *data, uint32 size)
{
	if ((address == DEVICE_ADDR) && (size == 4U)) {
		memcpy(&device_value, data, 4U);
		device_writes++;
		stm32_device_accessed = TRUE;
		return STD_E_OK;
	}
	if (!holds(RAM_BASE, RAM_SIZE, (uint32)address, size)) {
		return STD_E_SEGV;
	}
	memcpy(&active_ram[address - RAM_BASE], data, size);
	return STD_E_OK;
}

Std_ReturnType mpu_get_data8(CoreIdType c, AthrillAddrType a, uint8 *d) { (void)c; return memory_read(a, d, 1U); }
Std_ReturnType mpu_get_data16(CoreIdType c, AthrillAddrType a, uint16 *d) { (void)c; return memory_read(a, d, 2U); }
Std_ReturnType mpu_get_data32(CoreIdType c, AthrillAddrType a, uint32 *d) { (void)c; return memory_read(a, d, 4U); }
Std_ReturnType mpu_put_data8(CoreIdType c, AthrillAddrType a, uint8 d) { (void)c; return memory_write(a, &d, 1U); }
Std_ReturnType mpu_put_data16(CoreIdType c, AthrillAddrType a, uint16 d) { (void)c; return memory_write(a, &d, 2U); }
Std_ReturnType mpu_put_data32(CoreIdType c, AthrillAddrType a, uint32 d) { (void)c; return memory_write(a, &d, 4U); }

Std_ReturnType cpuemu_get_devcfg_value(const char *key, uint32 *value)
{
	(void)key;
	(void)value;
	return STD_E_NOENT;
}
int athrill_host_function_find(AthrillAddrType address) { (void)address; return -1; }
std_bool athrill_host_function_invoke(CoreIdType id, int function_id) { (void)id; (void)function_id; return FALSE; }
AthrillAddrType athrill_idle_spin_halt_idle_pc(void) { return 0U; }
std_bool athrill_idle_spin_halt_idle_ready(CoreIdType id) { (void)id; return FALSE; }
void athrill_idle_spin_halt_after_execute(CoreIdType id, AthrillAddrType pc) { (void)id; (void)pc; }
void cpu_trace_pc_record(uint32 pc) { (void)pc; }

static void put16(uint32 address, uint16 value)
{
	uint32 off = address - ROM_BASE;
	rom[off] = (uint8)value;
	rom[off + 1U] = (uint8)(value >> 8);
}

static void put32(uint32 address, uint32 value)
{
	memcpy(&rom[address - ROM_BASE], &value, 4U);
}

static void put_bl(uint32 pc, uint32 target)
{
	uint32 imm = target - (pc + 4U);
	uint32 s = (imm >> 24) & 1U;
	uint32 i1 = (imm >> 23) & 1U;
	uint32 i2 = (imm >> 22) & 1U;
	uint32 j1 = (i1 ^ s) ^ 1U;
	uint32 j2 = (i2 ^ s) ^ 1U;

	put16(pc, (uint16)(0xF000U | (s << 10) | ((imm >> 12) & 0x3FFU)));
	put16(pc + 2U, (uint16)(0xD000U | (j1 << 13) | (j2 << 11) | ((imm >> 1) & 0x7FFU)));
}

static void reset_windows(void)
{
	memset(memory_windows, 0, sizeof(memory_windows));
	memset(&memory_code_window, 0, sizeof(memory_code_window));
}

static void init_core(TargetCoreType *core, uint32 pc)
{
	uint32 i;

	memset(core, 0, sizeof(*core));
	ArmMReset(&core->m, 32U, 4U, TRUE);
	core->reg.pc = pc;
	core->reg.next_pc = pc;
	core->reg.r[13] = RAM_BASE + RAM_SIZE - 0x20U;
	core->reg.r[14] = (CODE_BASE + 0x301U);
	core->reg.apsr = ARM_APSR_Q | (0xAU << ARM_APSR_BITPOS_GE);
	core->reg.fpscr = 0x01C00013U;
	for (i = 0U; i < 32U; i++) {
		core->reg.s[i] = 0x01010101U * i;
	}
	core->m.vtor = ROM_BASE;
}

static int same_result(const char *name, Std_ReturnType ea, Std_ReturnType eb,
	uint64 ca, uint64 cb, uint64 xa, uint64 xb, uint32 dva, uint32 dvb,
	uint32 dwa, uint32 dwb, const TargetCoreType *a, const TargetCoreType *b)
{
	checks++;
	if ((ea == eb) && (ca == cb) && (xa == xb) && (dva == dvb) && (dwa == dwb)
		&& (memcmp(a, b, sizeof(*a)) == 0) && (memcmp(ram_a, ram_b, RAM_SIZE) == 0)) {
		return 1;
	}
	failures++;
	if (failures <= 20) {
		printf("FAIL %s: err %d/%d clock %llu/%llu executed %llu/%llu pc %08x/%08x next %08x/%08x apsr %08x/%08x device %08x/%08x writes %u/%u mem %s\n",
			name, ea, eb, (unsigned long long)ca, (unsigned long long)cb,
			(unsigned long long)xa, (unsigned long long)xb, a->reg.pc, b->reg.pc,
			a->reg.next_pc, b->reg.next_pc, a->reg.apsr, b->reg.apsr,
			dva, dvb, dwa, dwb, memcmp(ram_a, ram_b, RAM_SIZE) ? "diff" : "same");
	}
	return 0;
}

static ThumbJitDiffStats run_pair(const char *name, const TargetCoreType *initial, uint64 budget)
{
	TargetCoreType a = *initial;
	TargetCoreType b = *initial;
	Std_ReturnType ea;
	Std_ReturnType eb;
	uint64 ca = 17U, cb = 17U, xa = 0U, xb = 0U;
	uint32 dva, dvb, dwa, dwb;
	ThumbJitDiffStats stats;

	memcpy(ram_a, ram_seed, RAM_SIZE);
	memcpy(ram_b, ram_seed, RAM_SIZE);
	thumb_jit_diff_reset();
	thumb_invalidate_code(ROM_BASE, ROM_SIZE);
	thumb_jit_diff_set_enabled(FALSE);
	active_ram = ram_a;
	reset_windows();
	stm32_device_accessed = FALSE;
	device_value = 0x13579BDFU;
	device_writes = 0U;
	ea = thumb_execute_run(&a, budget, &ca, &xa);
	dva = device_value;
	dwa = device_writes;
	thumb_jit_diff_set_enabled(TRUE);
	active_ram = ram_b;
	reset_windows();
	stm32_device_accessed = FALSE;
	device_value = 0x13579BDFU;
	device_writes = 0U;
	eb = thumb_execute_run(&b, budget, &cb, &xb);
	dvb = device_value;
	dwb = device_writes;
	same_result(name, ea, eb, ca, cb, xa, xb, dva, dvb, dwa, dwb, &a, &b);
	last_interpreter = a;
	last_jit = b;
	thumb_jit_diff_get_stats(&stats);
	return stats;
}

static void require_counter(const char *name, const ThumbJitDiffStats *stats, uint32 which)
{
	if (stats->native[which] == 0U) {
		printf("FAIL %s: native counter %u was not reached\n", name, which);
		failures++;
	}
}

static uint16 mov_i(uint32 d, uint32 imm) { return (uint16)(0x2000U | (d << 8) | imm); }
static uint16 mov_r(uint32 d, uint32 m) { return (uint16)(0x4600U | ((d & 8U) << 4) | (m << 3) | (d & 7U)); }
static uint16 cmp_i(uint32 n, uint32 imm) { return (uint16)(0x2800U | (n << 8) | imm); }
static uint16 cmp_r(uint32 n, uint32 m) { return (uint16)(0x4280U | (m << 3) | n); }
static uint16 add_i3(uint32 d, uint32 n, uint32 imm) { return (uint16)(0x1C00U | (imm << 6) | (n << 3) | d); }
static uint16 sub_i3(uint32 d, uint32 n, uint32 imm) { return (uint16)(0x1E00U | (imm << 6) | (n << 3) | d); }
static uint16 add_i8(uint32 d, uint32 imm) { return (uint16)(0x3000U | (d << 8) | imm); }
static uint16 sub_i8(uint32 d, uint32 imm) { return (uint16)(0x3800U | (d << 8) | imm); }
static uint16 add_r(uint32 d, uint32 n, uint32 m) { return (uint16)(0x1800U | (m << 6) | (n << 3) | d); }
static uint16 sub_r(uint32 d, uint32 n, uint32 m) { return (uint16)(0x1A00U | (m << 6) | (n << 3) | d); }
static uint16 ldr_i(uint32 t, uint32 n, uint32 imm5) { return (uint16)(0x6800U | (imm5 << 6) | (n << 3) | t); }
static uint16 str_i(uint32 t, uint32 n, uint32 imm5) { return (uint16)(0x6000U | (imm5 << 6) | (n << 3) | t); }

static void one_native(const char *name, TargetCoreType *core, uint16 insn, uint32 counter)
{
	ThumbJitDiffStats stats;

	put16(core->reg.pc, insn);
	put16(core->reg.pc + 2U, 0xBF00U);
	stats = run_pair(name, core, 1U);
	require_counter(name, &stats, counter);
}

static void test_data(void)
{
	static const uint32 edge[] = { 0U, 1U, 0x7FFFFFFFU, 0x80000000U, 0xFFFFFFFFU };
	static const uint32 imm8[] = { 0U, 1U, 0x7FU, 0x80U, 0xFFU };
	TargetCoreType core;
	uint32 i, j;

	for (i = 0U; i < 5U; i++) {
		init_core(&core, CODE_BASE);
		core.reg.apsr |= (i & 0xFU) << 28;
		one_native("MOVS immediate", &core, mov_i(2U, imm8[i]), THUMB_JIT_DIFF_MOV_I);
		init_core(&core, CODE_BASE);
		core.reg.r[3] = edge[i];
		one_native("MOV register", &core, mov_r(9U, 3U), THUMB_JIT_DIFF_MOV_R);
		init_core(&core, CODE_BASE + 2U);
		one_native("MOV from PC", &core, mov_r(4U, 15U), THUMB_JIT_DIFF_MOV_R);
		for (j = 0U; j < 5U; j++) {
			init_core(&core, CODE_BASE);
			core.reg.r[1] = edge[i];
			core.reg.r[2] = edge[j];
			one_native("CMP immediate", &core, cmp_i(1U, imm8[j]), THUMB_JIT_DIFF_CMP_I);
			one_native("CMP register", &core, cmp_r(1U, 2U), THUMB_JIT_DIFF_CMP_R);
			one_native("ADDS imm3", &core, add_i3(1U, 1U, j & 7U), THUMB_JIT_DIFF_ADD_I3);
			one_native("SUBS imm3", &core, sub_i3(1U, 1U, j & 7U), THUMB_JIT_DIFF_SUB_I3);
			one_native("ADDS imm8", &core, add_i8(1U, imm8[j]), THUMB_JIT_DIFF_ADD_I8);
			one_native("SUBS imm8", &core, sub_i8(1U, imm8[j]), THUMB_JIT_DIFF_SUB_I8);
			one_native("ADDS register", &core, add_r(1U, 1U, 2U), THUMB_JIT_DIFF_ADD_R);
			one_native("SUBS register", &core, sub_r(1U, 1U, 2U), THUMB_JIT_DIFF_SUB_R);
			core.reg.r[2] = core.reg.r[1];
			one_native("ADDS Rn equals Rm", &core, add_r(1U, 2U, 2U), THUMB_JIT_DIFF_ADD_R);
			one_native("SUBS Rn equals Rm", &core, sub_r(1U, 2U, 2U), THUMB_JIT_DIFF_SUB_R);
		}
	}
}

static void test_branches(void)
{
	TargetCoreType core;
	ThumbJitDiffStats stats;
	uint32 cond, nzcv, value;

	for (cond = 0U; cond < 14U; cond++) {
		for (nzcv = 0U; nzcv < 16U; nzcv++) {
			init_core(&core, CODE_BASE);
			core.reg.apsr = (core.reg.apsr & 0x0FFFFFFFU) | (nzcv << 28);
			put16(CODE_BASE, (uint16)(0xD000U | (cond << 8) | 1U));
			stats = run_pair("B conditional", &core, 1U);
			require_counter("B conditional", &stats, THUMB_JIT_DIFF_B_COND);
		}
	}
	for (value = 0U; value < 2U; value++) {
		init_core(&core, CODE_BASE);
		core.reg.r[3] = value;
		put16(CODE_BASE, (uint16)(0xB100U | (1U << 3) | 3U));
		stats = run_pair("CBZ", &core, 1U);
		require_counter("CBZ", &stats, THUMB_JIT_DIFF_CBZ);
		put16(CODE_BASE, (uint16)(0xB900U | (1U << 3) | 3U));
		stats = run_pair("CBNZ", &core, 1U);
		require_counter("CBNZ", &stats, THUMB_JIT_DIFF_CBNZ);
	}
	init_core(&core, CODE_BASE);
	put16(CODE_BASE, 0xE002U);
	stats = run_pair("B forward", &core, 1U);
	require_counter("B forward", &stats, THUMB_JIT_DIFF_B);
	init_core(&core, CODE_BASE + 8U);
	put16(CODE_BASE + 8U, 0xE7FAU);
	stats = run_pair("B backward", &core, 1U);
	require_counter("B backward", &stats, THUMB_JIT_DIFF_B);
	init_core(&core, CODE_BASE);
	put_bl(CODE_BASE, CODE_BASE + 12U);
	stats = run_pair("BL forward", &core, 1U);
	require_counter("BL forward", &stats, THUMB_JIT_DIFF_BL);
	if (last_interpreter.reg.r[14] != last_jit.reg.r[14]) {
		printf("FAIL BL forward LR differs\n"); failures++;
	}
	init_core(&core, CODE_BASE + 16U);
	put_bl(CODE_BASE + 16U, CODE_BASE);
	stats = run_pair("BL backward", &core, 1U);
	require_counter("BL backward", &stats, THUMB_JIT_DIFF_BL);
}

static void test_literals(void)
{
	TargetCoreType core;
	ThumbJitDiffStats stats;

	init_core(&core, CODE_BASE);
	put16(CODE_BASE, 0x4800U);
	put32(CODE_BASE + 4U, 0x89ABCDEFU);
	stats = run_pair("LDR literal aligned PC", &core, 1U);
	require_counter("LDR literal aligned PC", &stats, THUMB_JIT_DIFF_LDR_LITERAL);
	init_core(&core, CODE_BASE + 2U);
	put16(CODE_BASE + 2U, 0x4900U);
	put32(CODE_BASE + 4U, 0x76543210U);
	stats = run_pair("LDR literal Align(PC,4)", &core, 1U);
	require_counter("LDR literal Align(PC,4)", &stats, THUMB_JIT_DIFF_LDR_LITERAL);
}

static void test_mixed(void)
{
	TargetCoreType core;
	ThumbJitDiffStats stats;
	uint32 sp;

	init_core(&core, CODE_BASE);
	core.reg.r[0] = RAM_BASE + 0x100U;
	put32(CODE_BASE + 0x100U, 0x11223344U);
	memcpy(&ram_seed[0x100U], "\x44\x33\x22\x11", 4U);
	put16(CODE_BASE, mov_i(2U, 5U));
	put16(CODE_BASE + 2U, ldr_i(3U, 0U, 0U));
	put16(CODE_BASE + 4U, add_i8(3U, 1U));
	put16(CODE_BASE + 6U, str_i(3U, 0U, 1U));
	put16(CODE_BASE + 8U, 0xB40CU); /* PUSH {r2,r3} */
	put16(CODE_BASE + 10U, 0xBC30U); /* POP {r4,r5} */
	put16(CODE_BASE + 12U, sub_i8(5U, 1U));
	stats = run_pair("mixed loads stores push pop", &core, 7U);
	require_counter("mixed MOVS", &stats, THUMB_JIT_DIFF_MOV_I);
	require_counter("mixed ADDS", &stats, THUMB_JIT_DIFF_ADD_I8);
	require_counter("mixed SUBS", &stats, THUMB_JIT_DIFF_SUB_I8);
	/* LDR/STR/PUSH/POP to RAM take the JIT's direct path (or are called) */
	if ((stats.called + stats.memory_native) < 4U) {
		printf("FAIL mixed memory count %llu called + %llu native\n", (unsigned long long)stats.called,
			(unsigned long long)stats.memory_native);
		failures++;
	}

	init_core(&core, CODE_BASE + 0x40U);
	core.reg.r[14] = CODE_BASE + 0x81U;
	put16(CODE_BASE + 0x40U, mov_i(0U, 7U));
	put16(CODE_BASE + 0x42U, 0x4770U); /* BX LR */
	put16(CODE_BASE + 0x80U, 0xBF00U);
	stats = run_pair("called BX writes PC", &core, 2U);
	if (stats.called == 0U) { printf("FAIL BX was not called\n"); failures++; }

	init_core(&core, CODE_BASE + 0x50U);
	sp = core.reg.r[13];
	memcpy(&ram_seed[sp - RAM_BASE], (uint32[]){ CODE_BASE + 0x91U }, 4U);
	put16(CODE_BASE + 0x50U, 0xBD00U); /* POP {pc} */
	put16(CODE_BASE + 0x90U, 0xBF00U);
	stats = run_pair("called POP pc", &core, 1U);
	if ((stats.called + stats.memory_native) == 0U) { printf("FAIL POP pc ran neither called nor native\n"); failures++; }

	init_core(&core, CODE_BASE + 0x60U);
	put16(CODE_BASE + 0x60U, mov_i(0U, 1U));
	put16(CODE_BASE + 0x62U, 0xDE00U); /* UDF #0: synchronous UsageFault */
	put16(CODE_BASE + 0x64U, mov_i(0U, 2U));
	stats = run_pair("UDF in middle", &core, 3U);
	if (stats.called == 0U) { printf("FAIL UDF was not called\n"); failures++; }

	init_core(&core, CODE_BASE + 0x70U);
	core.reg.r[0] = DEVICE_ADDR;
	put16(CODE_BASE + 0x70U, mov_i(1U, 0x5AU));
	put16(CODE_BASE + 0x72U, str_i(1U, 0U, 0U));
	put16(CODE_BASE + 0x74U, mov_i(2U, 0xEEU));
	stats = run_pair("device store stops block", &core, 3U);
	if ((last_interpreter.reg.pc != CODE_BASE + 0x74U) || (stats.called == 0U)) {
		printf("FAIL device store did not stop at the following instruction\n"); failures++;
	}
}

static void test_it(void)
{
	TargetCoreType core;
	ThumbJitDiffStats stats;
	uint64 native_total = 0U;
	uint32 i;

	init_core(&core, CODE_BASE + 0xA0U);
	core.reg.apsr |= ARM_APSR_Z;
	put16(CODE_BASE + 0xA0U, 0xBF08U); /* IT EQ */
	put16(CODE_BASE + 0xA2U, mov_i(0U, 1U));
	stats = run_pair("IT EQ", &core, 2U);
	for (i = 0U; i < THUMB_JIT_DIFF_NATIVE_COUNT; i++) native_total += stats.native[i];
	if ((stats.called == 0U) || (stats.it_fallbacks == 0U) || (native_total != 0U)) {
		printf("FAIL IT EQ path: called=%llu fallback=%llu native=%llu\n",
			(unsigned long long)stats.called, (unsigned long long)stats.it_fallbacks,
			(unsigned long long)native_total); failures++;
	}

	init_core(&core, CODE_BASE + 0xB0U);
	core.reg.apsr |= ARM_APSR_Z;
	put16(CODE_BASE + 0xB0U, 0xBF06U); /* ITTE EQ */
	put16(CODE_BASE + 0xB2U, mov_i(0U, 3U));
	put16(CODE_BASE + 0xB4U, add_i8(1U, 4U));
	put16(CODE_BASE + 0xB6U, sub_i8(2U, 5U));
	stats = run_pair("ITTE conditional instructions", &core, 4U);
	native_total = 0U;
	for (i = 0U; i < THUMB_JIT_DIFF_NATIVE_COUNT; i++) native_total += stats.native[i];
	if ((stats.it_fallbacks < 3U) || (native_total != 0U)) {
		printf("FAIL ITTE ran inside JIT: fallback=%llu native=%llu\n",
			(unsigned long long)stats.it_fallbacks, (unsigned long long)native_total); failures++;
	}
}

typedef struct {
	TargetCoreType a;
	TargetCoreType b;
	uint64 ca;
	uint64 cb;
} PersistentPair;

static void persistent_begin(PersistentPair *pair, const TargetCoreType *initial)
{
	pair->a = *initial;
	pair->b = *initial;
	pair->ca = 31U;
	pair->cb = 31U;
	memcpy(ram_a, ram_seed, RAM_SIZE);
	memcpy(ram_b, ram_seed, RAM_SIZE);
	thumb_jit_diff_reset();
	thumb_invalidate_code(ROM_BASE, ROM_SIZE);
}

static void persistent_call(PersistentPair *pair, const char *name, uint64 budget)
{
	Std_ReturnType ea, eb;
	uint64 xa = 0U, xb = 0U;

	thumb_jit_diff_set_enabled(FALSE);
	active_ram = ram_a; reset_windows(); stm32_device_accessed = FALSE;
	device_value = 0x13579BDFU; device_writes = 0U;
	ea = thumb_execute_run(&pair->a, budget, &pair->ca, &xa);
	thumb_jit_diff_set_enabled(TRUE);
	active_ram = ram_b; reset_windows(); stm32_device_accessed = FALSE;
	device_value = 0x13579BDFU; device_writes = 0U;
	eb = thumb_execute_run(&pair->b, budget, &pair->cb, &xb);
	same_result(name, ea, eb, pair->ca, pair->cb, xa, xb,
		device_value, device_value, device_writes, device_writes, &pair->a, &pair->b);
}

static void test_chaining(void)
{
	TargetCoreType core;
	PersistentPair pair;
	ThumbJitDiffStats stats;
	uint32 i;
	uint32 base = CODE_BASE + 0xC0U;

	put16(base, add_i8(0U, 1U)); put16(base + 2U, 0xE001U);
	put16(base + 8U, sub_i8(1U, 1U)); put16(base + 10U, 0xE001U);
	put16(base + 16U, cmp_i(0U, 3U)); put16(base + 18U, 0xD1F5U);
	init_core(&core, base);
	core.reg.r[1] = 9U;
	persistent_begin(&pair, &core);
	for (i = 0U; i < 6U; i++) persistent_call(&pair, "chained boundary budget", 2U);
	thumb_jit_diff_get_stats(&stats);
	if ((stats.chain_links < 2U) || (stats.block_entries <= stats.runs)) {
		printf("FAIL chaining: links=%u entries=%llu C-runs=%llu\n", stats.chain_links,
			(unsigned long long)stats.block_entries, (unsigned long long)stats.runs); failures++;
	}
}

static uint64 random_state = 0xA17E5EED5A17D1FFULL;
static uint32 random32(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 7;
	random_state ^= random_state << 17;
	return (uint32)(random_state >> 16);
}

static void test_random(void)
{
	const uint64 seed = random_state;
	uint32 program;
	uint64 native_seen = 0U, called_seen = 0U;

	for (program = 0U; program < 2000U; program++) {
		TargetCoreType core;
		PersistentPair pair;
		ThumbJitDiffStats stats;
		char label[96];
		uint32 base = CODE_BASE + 0x400U;
		uint32 count = 6U + (random32() % 7U);
		uint32 i, pc = base;

		for (i = 0U; i < count; i++, pc += 2U) {
			uint32 d = random32() % 6U;
			uint32 n = random32() % 6U;
			uint32 m = random32() % 6U;
			switch (random32() % 12U) {
			case 0: put16(pc, mov_i(d, random32() & 0xFFU)); break;
			case 1: put16(pc, mov_r(8U + (d & 3U), m)); break;
			case 2: put16(pc, cmp_i(n, random32() & 0xFFU)); break;
			case 3: put16(pc, cmp_r(n, m)); break;
			case 4: put16(pc, add_i3(d, n, random32() & 7U)); break;
			case 5: put16(pc, sub_i3(d, n, random32() & 7U)); break;
			case 6: put16(pc, add_i8(d, random32() & 0xFFU)); break;
			case 7: put16(pc, sub_i8(d, random32() & 0xFFU)); break;
			case 8: put16(pc, add_r(d, n, m)); break;
			case 9: put16(pc, sub_r(d, n, m)); break;
			case 10: put16(pc, ldr_i(d, 6U, random32() & 7U)); break;
			default: put16(pc, str_i(d, 6U, random32() & 7U)); break;
			}
		}
		if ((program & 1U) == 0U) {
			uint32 off = (base - (pc + 4U)) >> 1;
			put16(pc, (uint16)(0xE000U | (off & 0x7FFU)));
		} else {
			uint32 off = (base - (pc + 4U)) >> 1;
			put16(pc, (uint16)(0xD100U | (off & 0xFFU)));
			put16(pc + 2U, (uint16)(0xE000U | (((base - (pc + 6U)) >> 1) & 0x7FFU)));
		}
		init_core(&core, base);
		core.reg.r[6] = RAM_BASE + 0x200U;
		for (i = 0U; i < 6U; i++) core.reg.r[i] = random32();
		core.reg.apsr |= (random32() & 0xFU) << 28;
		persistent_begin(&pair, &core);
		for (i = 0U; i < 4U; i++) {
			snprintf(label, sizeof(label), "random seed=%016llx program=%u call=%u",
				(unsigned long long)seed, program, i);
			persistent_call(&pair, label, 1U + (random32() % 16U));
		}
		thumb_jit_diff_get_stats(&stats);
		called_seen += stats.called + stats.memory_native;
		for (i = 0U; i < THUMB_JIT_DIFF_NATIVE_COUNT; i++) native_seen += stats.native[i];
	}
	if ((native_seen == 0U) || (called_seen == 0U)) {
		printf("FAIL random path proof seed=%016llx native=%llu called=%llu\n",
			(unsigned long long)seed, (unsigned long long)native_seen,
			(unsigned long long)called_seen); failures++;
	}
	printf("random seed=%016llx programs=2000 native=%llu called=%llu\n",
		(unsigned long long)seed, (unsigned long long)native_seen,
		(unsigned long long)called_seen);
}

int main(void)
{
	memset(rom, 0, sizeof(rom));
	memset(ram_seed, 0xA5, sizeof(ram_seed));
	put32(ROM_BASE + 3U * 4U, CODE_BASE + 0x201U);
	put32(ROM_BASE + 6U * 4U, CODE_BASE + 0x221U);
	test_data();
	test_branches();
	test_literals();
	test_mixed();
	test_it();
	test_chaining();
	test_random();
	printf("stm32-jit-diff: %llu comparisons, %d failures\n",
		(unsigned long long)checks, failures);
	return failures == 0 ? 0 : 1;
}

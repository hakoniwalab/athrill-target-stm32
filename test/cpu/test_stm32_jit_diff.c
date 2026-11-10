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
static uint64 memory_cases;
static uint64 memory_native_cases;
static uint64 memory_called_cases;
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
std_bool athrill_idle_spin_halt_spin_enabled(void) { return FALSE; }
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

/* The decoder represents a 32-bit Thumb instruction as first:second. */
static void put_insn32(uint32 address, uint32 insn)
{
	put16(address, (uint16)(insn >> 16));
	put16(address + 2U, (uint16)insn);
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
static uint16 ldrb_i(uint32 t, uint32 n, uint32 imm5) { return (uint16)(0x7800U | (imm5 << 6) | (n << 3) | t); }
static uint16 ldrh_i(uint32 t, uint32 n, uint32 imm5) { return (uint16)(0x8800U | (imm5 << 6) | (n << 3) | t); }
static uint16 ldr_reg16(uint32 op, uint32 t, uint32 n, uint32 m)
{
	return (uint16)(0x5000U | (op << 9) | (m << 6) | (n << 3) | t);
}

static uint32 wide_imm12(uint16 first, uint32 n, uint32 t, uint32 imm12)
{
	return (((uint32)(first | n)) << 16) | (t << 12) | imm12;
}

static uint32 wide_imm8(uint16 first, uint32 n, uint32 t, uint32 mode, uint32 imm8)
{
	return (((uint32)(first | n)) << 16) | (t << 12) | (mode << 8) | imm8;
}

static uint32 wide_reg(uint16 first, uint32 n, uint32 t, uint32 m, uint32 shift)
{
	return (((uint32)(first | n)) << 16) | (t << 12) | (shift << 4) | m;
}

static ThumbJitDiffStats run_memory_case(const char *name, TargetCoreType *core,
	uint32 insn, uint32 length, uint32 memory_class)
{
	ThumbJitDiffStats stats;
	uint64 native;

	if (length == 2U) {
		put16(core->reg.pc, (uint16)insn);
	} else {
		put_insn32(core->reg.pc, insn);
	}
	put16(core->reg.pc + length, 0xBF00U);
	stats = run_pair(name, core, 1U);
	native = stats.memory_native_class[memory_class];
	memory_cases++;
	memory_native_cases += native;
	memory_called_cases += stats.called;
	if ((native + stats.called) != 1U) {
		printf("FAIL %s: memory path native=%llu called=%llu\n", name,
			(unsigned long long)native, (unsigned long long)stats.called);
		failures++;
	}
	printf("memory path %s: %s\n", name, native != 0U ? "native" : "called");
	return stats;
}

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

static void seed_load_value(uint32 address, uint32 size, std_bool top_set)
{
	uint32 off = address - RAM_BASE;

	if (size == 1U) {
		ram_seed[off] = top_set ? 0x80U : 0x7FU;
	} else {
		ram_seed[off] = 0x34U;
		ram_seed[off + 1U] = top_set ? 0x80U : 0x7FU;
	}
}

static void test_load_extensions(void)
{
	typedef struct {
		const char *name;
		uint16 insn;
		uint32 address;
		uint32 size;
		uint32 reg_offset;
	} NarrowLoad;
	static const NarrowLoad narrow[] = {
		{ "LDRB imm T1", 0x7842U, RAM_BASE + 0x301U, 1U, 0U },
		{ "LDRH imm T1", 0x8842U, RAM_BASE + 0x302U, 2U, 0U },
		{ "LDRB reg T1", 0x5C42U, RAM_BASE + 0x301U, 1U, 1U },
		{ "LDRH reg T1", 0x5A42U, RAM_BASE + 0x302U, 2U, 2U },
		{ "LDRSB reg T1", 0x5642U, RAM_BASE + 0x301U, 1U, 1U },
		{ "LDRSH reg T1", 0x5E42U, RAM_BASE + 0x302U, 2U, 2U }
	};
	static const struct {
		const char *name;
		uint16 imm_first;
		uint16 reg_first;
		uint32 size;
	} wide[] = {
		{ "LDRB", 0xF890U, 0xF810U, 1U },
		{ "LDRH", 0xF8B0U, 0xF830U, 2U },
		{ "LDRSB", 0xF990U, 0xF910U, 1U },
		{ "LDRSH", 0xF9B0U, 0xF930U, 2U }
	};
	TargetCoreType core;
	char name[96];
	uint32 i, set;
	uint32 base = RAM_BASE + 0x300U;

	for (i = 0U; i < sizeof(narrow) / sizeof(narrow[0]); i++) {
		for (set = 0U; set < 2U; set++) {
			seed_load_value(narrow[i].address, narrow[i].size, set != 0U);
			init_core(&core, CODE_BASE + 0x1000U);
			core.reg.r[0] = base;
			core.reg.r[1] = narrow[i].reg_offset;
			snprintf(name, sizeof(name), "%s top-bit-%s", narrow[i].name, set ? "set" : "clear");
			run_memory_case(name, &core, narrow[i].insn, 2U, THUMB_JIT_DIFF_MEMORY_LOAD);
		}
	}
	for (i = 0U; i < sizeof(wide) / sizeof(wide[0]); i++) {
		for (set = 0U; set < 2U; set++) {
			uint32 t = set ? 8U : 2U;
			uint32 address = base + 0x20U;

			seed_load_value(address, wide[i].size, set != 0U);
			init_core(&core, CODE_BASE + 0x1000U);
			core.reg.r[7] = base;
			snprintf(name, sizeof(name), "%s imm12 wide R%u top-bit-%s", wide[i].name, t,
				set ? "set" : "clear");
			run_memory_case(name, &core, wide_imm12(wide[i].imm_first, 7U, t, 0x20U), 4U,
				THUMB_JIT_DIFF_MEMORY_LOAD);

			init_core(&core, CODE_BASE + 0x1000U);
			core.reg.r[7] = base;
			core.reg.r[6] = 0x20U;
			snprintf(name, sizeof(name), "%s reg wide R%u top-bit-%s", wide[i].name, t,
				set ? "set" : "clear");
			run_memory_case(name, &core, wide_reg(wide[i].reg_first, 7U, t, 6U, 0U), 4U,
				THUMB_JIT_DIFF_MEMORY_LOAD);
		}
	}
	/* The signed wide immediate T2 forms also have an imm8 negative-offset encoding. */
	for (i = 2U; i < 4U; i++) {
		for (set = 0U; set < 2U; set++) {
			uint32 t = set ? 9U : 3U;
			uint32 address = RAM_BASE + 0x380U;

			seed_load_value(address, wide[i].size, set != 0U);
			init_core(&core, CODE_BASE + 0x1000U);
			core.reg.r[7] = address + 12U;
			snprintf(name, sizeof(name), "%s imm8 offset wide R%u top-bit-%s", wide[i].name, t,
				set ? "set" : "clear");
			run_memory_case(name, &core, wide_imm8(wide[i].reg_first, 7U, t, 0xCU, 12U), 4U,
				THUMB_JIT_DIFF_MEMORY_LOAD);
		}
	}
}

static void test_wide_immediate(void)
{
	static const struct {
		const char *name;
		uint16 imm12_first;
		uint16 imm8_first;
		uint32 memory_class;
	} op[] = {
		{ "LDR.W", 0xF8D0U, 0xF850U, THUMB_JIT_DIFF_MEMORY_LOAD },
		{ "STR.W", 0xF8C0U, 0xF840U, THUMB_JIT_DIFF_MEMORY_STORE },
		{ "LDRB.W", 0xF890U, 0xF810U, THUMB_JIT_DIFF_MEMORY_LOAD },
		{ "STRB.W", 0xF880U, 0xF800U, THUMB_JIT_DIFF_MEMORY_STORE },
		{ "LDRH.W", 0xF8B0U, 0xF830U, THUMB_JIT_DIFF_MEMORY_LOAD },
		{ "STRH.W", 0xF8A0U, 0xF820U, THUMB_JIT_DIFF_MEMORY_STORE }
	};
	static const struct {
		const char *name;
		uint32 mode;
		sint32 base_adjust;
	} mode[] = {
		{ "offset-sub", 0xCU, 12 }, { "pre-add-wb", 0xFU, -12 },
		{ "pre-sub-wb", 0xDU, 12 }, { "post-add-wb", 0xBU, 0 },
		{ "post-sub-wb", 0x9U, 0 }
	};
	TargetCoreType core;
	char name[96];
	uint32 i, j;
	uint32 target = RAM_BASE + 0x500U;

	memcpy(&ram_seed[target - RAM_BASE], (uint32[]){ 0x89ABCDEFU, 0x01234567U }, 8U);
	for (i = 0U; i < sizeof(op) / sizeof(op[0]); i++) {
		init_core(&core, CODE_BASE + 0x1100U);
		core.reg.r[7] = target - 0x20U;
		core.reg.r[8] = 0xD6C5B4A3U;
		snprintf(name, sizeof(name), "%s imm12 T3", op[i].name);
		run_memory_case(name, &core, wide_imm12(op[i].imm12_first, 7U, 8U, 0x20U), 4U,
			op[i].memory_class);
		for (j = 0U; j < sizeof(mode) / sizeof(mode[0]); j++) {
			init_core(&core, CODE_BASE + 0x1100U);
			core.reg.r[7] = target + (uint32)mode[j].base_adjust;
			core.reg.r[8] = 0xD6C5B4A3U;
			snprintf(name, sizeof(name), "%s imm8 T4 %s", op[i].name, mode[j].name);
			run_memory_case(name, &core,
				wide_imm8(op[i].imm8_first, 7U, 8U, mode[j].mode, 12U), 4U,
				op[i].memory_class);
		}
	}
}

static void test_wide_register_dual_multiple(void)
{
	static const struct {
		const char *name;
		uint32 insn;
		sint32 base_adjust;
		uint32 memory_class;
	} dual[] = {
		{ "LDRD offset add", 0xE9D78904U, -16, THUMB_JIT_DIFF_MEMORY_LOAD },
		{ "LDRD offset subtract", 0xE9578904U, 16, THUMB_JIT_DIFF_MEMORY_LOAD },
		{ "LDRD pre-index add write-back", 0xE9F78904U, -16, THUMB_JIT_DIFF_MEMORY_LOAD },
		{ "LDRD pre-index subtract write-back", 0xE9778904U, 16, THUMB_JIT_DIFF_MEMORY_LOAD },
		{ "LDRD post-index add write-back", 0xE8F78904U, 0, THUMB_JIT_DIFF_MEMORY_LOAD },
		{ "LDRD post-index subtract write-back", 0xE8778904U, 0, THUMB_JIT_DIFF_MEMORY_LOAD },
		{ "STRD offset add", 0xE9C78904U, -16, THUMB_JIT_DIFF_MEMORY_STORE },
		{ "STRD offset subtract", 0xE9478904U, 16, THUMB_JIT_DIFF_MEMORY_STORE },
		{ "STRD pre-index add write-back", 0xE9E78904U, -16, THUMB_JIT_DIFF_MEMORY_STORE },
		{ "STRD pre-index subtract write-back", 0xE9678904U, 16, THUMB_JIT_DIFF_MEMORY_STORE },
		{ "STRD post-index add write-back", 0xE8E78904U, 0, THUMB_JIT_DIFF_MEMORY_STORE },
		{ "STRD post-index subtract write-back", 0xE8678904U, 0, THUMB_JIT_DIFF_MEMORY_STORE }
	};
	TargetCoreType core;
	char name[80];
	uint32 i;
	uint32 target = RAM_BASE + 0x600U;
	uint32 sp;

	memcpy(&ram_seed[target - RAM_BASE], (uint32[]){ 0x10213243U, 0x90A1B2C3U, 0x55667788U }, 12U);
	for (i = 0U; i < 4U; i++) {
		init_core(&core, CODE_BASE + 0x1200U);
		core.reg.r[7] = target - (1U << i);
		core.reg.r[6] = 1U;
		snprintf(name, sizeof(name), "LDR.W register LSL #%u", i);
		run_memory_case(name, &core, wide_reg(0xF850U, 7U, 8U, 6U, i), 4U,
			THUMB_JIT_DIFF_MEMORY_LOAD);
	}
	for (i = 0U; i < sizeof(dual) / sizeof(dual[0]); i++) {
		init_core(&core, CODE_BASE + 0x1200U);
		core.reg.r[7] = target + (uint32)dual[i].base_adjust;
		core.reg.r[8] = 0x44332211U;
		core.reg.r[9] = 0x88776655U;
		run_memory_case(dual[i].name, &core, dual[i].insn, 4U, dual[i].memory_class);
	}

	init_core(&core, CODE_BASE + 0x1200U);
	core.reg.r[7] = target;
	run_memory_case("LDRD Rt equals Rt2 UNPREDICTABLE", &core, 0xE9D78804U, 4U,
		THUMB_JIT_DIFF_MEMORY_LOAD);
	init_core(&core, CODE_BASE + 0x1200U);
	core.reg.r[7] = target;
	run_memory_case("LDR.W base equals loaded register", &core, 0xF8D77000U, 4U,
		THUMB_JIT_DIFF_MEMORY_LOAD);
	init_core(&core, CODE_BASE + 0x1200U);
	core.reg.r[7] = target;
	run_memory_case("LDR.W write-back base equals Rt UNPREDICTABLE", &core, 0xF8577B04U, 4U,
		THUMB_JIT_DIFF_MEMORY_LOAD);

	/* LDMIA without/with write-back, with PC, and the Rn-in-list UNPREDICTABLE form. */
	init_core(&core, CODE_BASE + 0x1200U);
	core.reg.r[7] = target;
	run_memory_case("LDMIA.W without write-back", &core, 0xE8970105U, 4U,
		THUMB_JIT_DIFF_MEMORY_LOAD);
	init_core(&core, CODE_BASE + 0x1200U);
	core.reg.r[7] = target;
	run_memory_case("LDMIA.W with write-back", &core, 0xE8B70105U, 4U,
		THUMB_JIT_DIFF_MEMORY_LOAD);
	memcpy(&ram_seed[target - RAM_BASE],
		(uint32[]){ 0x01020304U, 0x11223344U, CODE_BASE + 0x1301U }, 12U);
	put16(CODE_BASE + 0x1300U, 0xBF00U);
	init_core(&core, CODE_BASE + 0x1200U);
	core.reg.r[7] = target;
	run_memory_case("LDMIA.W with PC", &core, 0xE8B78005U, 4U,
		THUMB_JIT_DIFF_MEMORY_LOAD);
	init_core(&core, CODE_BASE + 0x1200U);
	core.reg.r[7] = target;
	run_memory_case("LDMIA.W write-back with Rn in list UNPREDICTABLE", &core, 0xE8B70181U, 4U,
		THUMB_JIT_DIFF_MEMORY_LOAD);

	init_core(&core, CODE_BASE + 0x1200U);
	core.reg.r[7] = target + 16U;
	core.reg.r[0] = 0x11111111U; core.reg.r[2] = 0x22222222U; core.reg.r[8] = 0x88888888U;
	run_memory_case("STMDB.W write-back", &core, 0xE9270105U, 4U,
		THUMB_JIT_DIFF_MEMORY_STORE);
	init_core(&core, CODE_BASE + 0x1200U);
	core.reg.r[0] = 0x11111111U; core.reg.r[2] = 0x22222222U; core.reg.r[8] = 0x88888888U;
	core.reg.r[14] = 0xEEEEEEEEU;
	run_memory_case("PUSH.W", &core, 0xE92D4105U, 4U, THUMB_JIT_DIFF_MEMORY_PUSH);

	init_core(&core, CODE_BASE + 0x1200U);
	sp = core.reg.r[13];
	memcpy(&ram_seed[sp - RAM_BASE], (uint32[]){ 0x10101010U, 0x20202020U, 0x80808080U }, 12U);
	run_memory_case("POP.W", &core, 0xE8BD0105U, 4U, THUMB_JIT_DIFF_MEMORY_POP);
	init_core(&core, CODE_BASE + 0x1200U);
	sp = core.reg.r[13];
	memcpy(&ram_seed[sp - RAM_BASE],
		(uint32[]){ 0x10101010U, 0x20202020U, CODE_BASE + 0x1301U }, 12U);
	run_memory_case("POP.W including PC", &core, 0xE8BD8005U, 4U, THUMB_JIT_DIFF_MEMORY_POP);
}

static void test_memory_boundaries_and_literals(void)
{
	TargetCoreType core;
	ThumbJitDiffStats stats;
	uint32 sp;
	uint32 pc = CODE_BASE + 0x1400U;

	ram_seed[0] = 0x81U;
	ram_seed[RAM_SIZE - 1U] = 0xFEU;
	init_core(&core, pc); core.reg.r[0] = RAM_BASE;
	run_memory_case("RAM first byte LDRB", &core, ldrb_i(1U, 0U, 0U), 2U, THUMB_JIT_DIFF_MEMORY_LOAD);
	init_core(&core, pc); core.reg.r[0] = RAM_BASE + RAM_SIZE - 1U;
	run_memory_case("RAM last byte LDRB", &core, ldrb_i(1U, 0U, 0U), 2U, THUMB_JIT_DIFF_MEMORY_LOAD);
	init_core(&core, pc); core.reg.r[0] = RAM_BASE + RAM_SIZE - 3U;
	run_memory_case("RAM end crossing LDR word", &core, ldr_i(1U, 0U, 0U), 2U,
		THUMB_JIT_DIFF_MEMORY_LOAD);

	init_core(&core, pc); core.reg.r[7] = RAM_BASE + 0x701U;
	run_memory_case("unaligned LDR.W word", &core, wide_imm12(0xF8D0U, 7U, 8U, 0U), 4U,
		THUMB_JIT_DIFF_MEMORY_LOAD);
	init_core(&core, pc); core.reg.r[7] = RAM_BASE + 0x703U;
	run_memory_case("unaligned LDRH.W halfword", &core, wide_imm12(0xF8B0U, 7U, 8U, 0U), 4U,
		THUMB_JIT_DIFF_MEMORY_LOAD);

	rom[0x1800U] = 0xE1U;
	init_core(&core, pc); core.reg.r[0] = ROM_BASE + 0x1800U;
	run_memory_case("ROM byte load", &core, ldrb_i(1U, 0U, 0U), 2U, THUMB_JIT_DIFF_MEMORY_LOAD);
	init_core(&core, pc); core.reg.r[0] = ROM_BASE + 0x1800U; core.reg.r[1] = 0x55U;
	run_memory_case("ROM byte store error", &core, 0x7001U, 2U, THUMB_JIT_DIFF_MEMORY_STORE);

	put32(pc + 0x20U, 0x31415926U);
	init_core(&core, pc);
	run_memory_case("LDR.W literal add", &core, 0xF8DF801CU, 4U, THUMB_JIT_DIFF_MEMORY_LOAD);
	put32(pc - 0x20U, 0x27182818U);
	init_core(&core, pc);
	run_memory_case("LDR.W literal subtract", &core, 0xF85F8024U, 4U, THUMB_JIT_DIFF_MEMORY_LOAD);

	/* Distinct values make a reversed native PUSH register order observable. */
	init_core(&core, pc);
	core.reg.r[0] = 0x00112233U; core.reg.r[2] = 0x44556677U; core.reg.r[7] = 0x8899AABBU;
	run_memory_case("PUSH T1 register order", &core, 0xB485U, 2U, THUMB_JIT_DIFF_MEMORY_PUSH);

	/* The odd stacked address must be written to PC with bit zero cleared. */
	init_core(&core, pc);
	sp = core.reg.r[13];
	memcpy(&ram_seed[sp - RAM_BASE], (uint32[]){ CODE_BASE + 0x1501U }, 4U);
	put16(CODE_BASE + 0x1500U, 0xBF00U);
	run_memory_case("POP T1 PC clears bit zero", &core, 0xBD00U, 2U, THUMB_JIT_DIFF_MEMORY_POP);

	/* A wide device store in the middle must return to C before the following instruction. */
	init_core(&core, pc);
	core.reg.r[7] = DEVICE_ADDR; core.reg.r[8] = 0xA1B2C3D4U;
	put16(pc, mov_i(0U, 1U));
	put_insn32(pc + 2U, wide_imm12(0xF8C0U, 7U, 8U, 0U));
	put16(pc + 6U, mov_i(0U, 2U));
	stats = run_pair("wide device store stops block", &core, 3U);
	if ((last_interpreter.reg.pc != pc + 6U) || (stats.called == 0U)) {
		printf("FAIL wide device store did not stop before following instruction\n");
		failures++;
	}
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

		for (i = 0U; i < count; i++) {
			uint32 d = random32() % 6U;
			uint32 n = random32() % 6U;
			uint32 m = random32() % 6U;
			uint32 choice = random32() % 24U;
			uint32 length = 2U;
			switch (choice) {
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
			case 11: put16(pc, str_i(d, 6U, random32() & 7U)); break;
			case 12: put16(pc, ldrb_i(d, 6U, random32() & 0x1FU)); break;
			case 13: put16(pc, ldrh_i(d, 6U, random32() & 0xFU)); break;
			case 14: put16(pc, ldr_reg16(3U, d, 6U, 7U)); break; /* LDRSB */
			case 15: put16(pc, ldr_reg16(7U, d, 6U, 7U)); break; /* LDRSH */
			case 16:
				put_insn32(pc, wide_imm12(0xF8D0U, 6U, d, random32() & 0x3FU)); length = 4U; break;
			case 17:
				put_insn32(pc, wide_imm12(0xF8C0U, 6U, d, random32() & 0x3FU)); length = 4U; break;
			case 18:
				put_insn32(pc, wide_imm12(0xF890U, 6U, d, random32() & 0x3FU)); length = 4U; break;
			case 19:
				put_insn32(pc, wide_imm12(0xF880U, 6U, d, random32() & 0x3FU)); length = 4U; break;
			case 20:
				put_insn32(pc, wide_imm12(0xF8B0U, 6U, d, random32() & 0x3FU)); length = 4U; break;
			case 21:
				put_insn32(pc, wide_imm12(0xF8A0U, 6U, d, random32() & 0x3FU)); length = 4U; break;
			case 22:
				put_insn32(pc, wide_reg(0xF850U, 6U, d, 7U, random32() & 3U)); length = 4U; break;
			default:
				put_insn32(pc, (random32() & 1U) ? 0xE9D60100U : 0xE9C60100U); length = 4U; break;
			}
			pc += length;
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
		core.reg.r[7] = random32() & 0xFU;
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
	test_load_extensions();
	test_wide_immediate();
	test_wide_register_dual_multiple();
	test_memory_boundaries_and_literals();
	test_mixed();
	test_it();
	test_chaining();
	test_random();
	printf("memory cases: %llu native=%llu called=%llu\n", (unsigned long long)memory_cases,
		(unsigned long long)memory_native_cases, (unsigned long long)memory_called_cases);
	printf("stm32-jit-diff: %llu comparisons, %d failures\n", (unsigned long long)checks, failures);
	return failures == 0 ? 0 : 1;
}

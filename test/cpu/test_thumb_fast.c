/*
 * The fast executors (src/cpu/thumb_fast.c) against the generic ones that go
 * through athrill-arm-common's pseudo-code: for each encoding with a fast
 * executor, random instructions of it run on random states (registers that
 * mostly point into a RAM window, flags, IT states, Thread or Handler mode),
 * once by each executor on its own copy of the state and of the memory; the
 * return value, the whole core state and the memory must be the same.
 */
#include <stdio.h>
#include <string.h>
#include "thumb_fast.h"
#include "memory_window.h"
#include "t32_decode16.h"
#include "t32_decode32.h"

/* stubs of the emulator: one RAM window, nothing else on the bus */
#define RAM_BASE	0x20000000U
#define RAM_SIZE	0x10000U

MemoryWindowType memory_windows[2];
MemoryWindowType memory_code_window;
std_bool stm32_exec_fast = TRUE;
std_bool stm32_fp_host_fast = TRUE;
static uint8 mem_a[RAM_SIZE];
static uint8 mem_b[RAM_SIZE];

uint8 *memory_window_lookup(uint32 addr, uint32 size)
{
	(void)addr;
	(void)size;
	return NULL;
}

uint8 *memory_window_code_lookup(uint32 addr, uint32 size)
{
	(void)addr;
	(void)size;
	return NULL;
}

Std_ReturnType mpu_get_data8(CoreIdType c, AthrillAddrType a, uint8 *d) { (void)c; (void)a; *d = 0U; return STD_E_SEGV; }
Std_ReturnType mpu_get_data16(CoreIdType c, AthrillAddrType a, uint16 *d) { (void)c; (void)a; *d = 0U; return STD_E_SEGV; }
Std_ReturnType mpu_get_data32(CoreIdType c, AthrillAddrType a, uint32 *d) { (void)c; (void)a; *d = 0U; return STD_E_SEGV; }
Std_ReturnType mpu_put_data8(CoreIdType c, AthrillAddrType a, uint8 d) { (void)c; (void)a; (void)d; return STD_E_SEGV; }
Std_ReturnType mpu_put_data16(CoreIdType c, AthrillAddrType a, uint16 d) { (void)c; (void)a; (void)d; return STD_E_SEGV; }
Std_ReturnType mpu_put_data32(CoreIdType c, AthrillAddrType a, uint32 d) { (void)c; (void)a; (void)d; return STD_E_SEGV; }

Std_ReturnType ArmExecuteFPCheck(TargetCoreType *core)
{
	(void)core;
	return STD_E_OK;
}

static uint64 rng = 0x2545F4914F6CDD1DULL;

static uint32 rnd(void)
{
	rng ^= rng << 13;
	rng ^= rng >> 7;
	rng ^= rng << 17;
	return (uint32)(rng >> 16);
}

static int failures;
static long checks;

/* A register value: mostly an address in the RAM window, sometimes anything. */
static uint32 reg_value(void)
{
	uint32 k = rnd() % 8U;

	if (k == 0U) {
		return rnd();
	}
	if (k == 1U) {
		return rnd() % 16U;
	}
	return RAM_BASE + 0x1000U + (rnd() % (RAM_SIZE - 0x3000U));
}

static void random_state(TargetCoreType *core)
{
	uint32 i;

	memset(core, 0, sizeof(*core));
	for (i = 0U; i < 15U; i++) {
		core->reg.r[i] = reg_value();
	}
	core->reg.pc = (RAM_BASE + 0x1000U + (rnd() % 0x8000U)) & ~1U;
	core->reg.apsr = rnd() & 0xF0000000U;
	/* outside an IT block mostly; else an IT state with 1 to 4 instructions left */
	core->reg.itstate = ((rnd() % 3U) == 0U) ? ((rnd() & 0xF0U) | (1U << (rnd() % 4U))) : 0U;
	if ((core->reg.itstate >> 4) >= 0xEU) {
		core->reg.itstate &= 0xDFU;	/* the conditions IT allows */
	}
	core->m.handler_mode = (rnd() & 1U) != 0U;
	core->m.epsr_t = TRUE;
	core->reg.next_pc = core->reg.pc + 2U;
}

static void fill_memory(void)
{
	uint32 i;

	for (i = 0U; i < RAM_SIZE; i++) {
		mem_a[i] = (uint8)rnd();
	}
	memcpy(mem_b, mem_a, RAM_SIZE);
}

static void use_memory(uint8 *mem)
{
	memory_windows[0].start = RAM_BASE;
	memory_windows[0].size = RAM_SIZE;
	memory_windows[0].data = mem;
	memory_windows[0].writable = TRUE;
	memory_windows[1] = memory_windows[0];
}

static void compare(const char *what, uint32 insn, Std_ReturnType ea, Std_ReturnType eb,
	const TargetCoreType *a, const TargetCoreType *b)
{
	checks++;
	if ((ea != eb) || (memcmp(a, b, sizeof(*a)) != 0) || (memcmp(mem_a, mem_b, RAM_SIZE) != 0)) {
		failures++;
		if (failures <= 20) {
			printf("FAIL %s insn %08x: err %d/%d pc %08x/%08x next %08x/%08x apsr %08x/%08x mem %s\n",
				what, insn, ea, eb, a->reg.pc, b->reg.pc, a->reg.next_pc, b->reg.next_pc,
				a->reg.apsr, b->reg.apsr, (memcmp(mem_a, mem_b, RAM_SIZE) == 0) ? "same" : "differs");
		}
	}
}

/* One instruction run by both executors from the same state and memory. */
static void run16(uint16 insn)
{
	TargetCoreType a;
	TargetCoreType b;
	Std_ReturnType ea;
	Std_ReturnType eb;

	random_state(&a);
	memcpy(&b, &a, sizeof(a));
	use_memory(mem_a);
	ea = thumb_executor16(insn)(&a, insn);
	use_memory(mem_b);
	eb = thumb_fast_executor16(insn)(&b, insn);
	compare(t32_decode16_names[t32_decode16(insn)], insn, ea, eb, &a, &b);
}

static void run32(uint32 insn)
{
	TargetCoreType a;
	TargetCoreType b;
	Std_ReturnType ea;
	Std_ReturnType eb;

	random_state(&a);
	a.reg.next_pc = a.reg.pc + 4U;
	memcpy(&b, &a, sizeof(a));
	use_memory(mem_a);
	ea = thumb_executor32(insn)(&a, insn);
	use_memory(mem_b);
	eb = thumb_fast_executor32(insn)(&b, insn);
	compare(t32_decode32_names[t32_decode32(insn)], insn, ea, eb, &a, &b);
}

/* Random instructions with a fast executor; the memory is refilled now and then (and after a difference). */
static void test(bool wide, uint32 tries)
{
	uint32 i;

	fill_memory();
	for (i = 0U; i < tries; i++) {
		int before = failures;

		if ((i % 4096U) == 0U) {
			fill_memory();
		}
		if (!wide) {
			uint16 insn = (uint16)rnd();

			if (thumb_fast_executor16(insn) != NULL) {
				run16(insn);
			}
		}
		else {
			uint32 insn = ((0x1DU + (rnd() % 3U)) << 27) | (rnd() & 0x07FFFFFFU);

			if (thumb_fast_executor32(insn) != NULL) {
				run32(insn);
			}
		}
		if (failures != before) {
			memcpy(mem_b, mem_a, RAM_SIZE);
		}
	}
}

int main(void)
{
	long before;

	test(FALSE, 4000000U);
	before = checks;
	test(TRUE, 40000000U);
	printf("16-bit cases %ld, 32-bit cases %ld\n", before, checks - before);
	printf("%ld checks, %d failures\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}

/*
 * Fetch and dispatch of Thumb instructions: a halfword whose bits 15:11 are
 * 0b11101, 0b11110 or 0b11111 starts a 32-bit instruction.
 */
#include "thumb_exec.h"
#include "t32_decode16.h"
#include "t32_decode32.h"
#include "arm_common/arm_m_exception.h"
#include "memory_window.h"
#include "thumb_fast.h"
#include "device.h"
#include "athrill_host_function_internal.h"
#include "athrill_idle_spin_halt.h"
#include <string.h>

static Std_ReturnType fetch(const TargetCoreType *core, uint32 address, uint32 *insn, uint32 *length)
{
	uint16 hw1;
	uint16 hw2;

	if (bus_get_data16(core->core_id, address, &hw1) != STD_E_OK) {
		return STD_E_SEGV;
	}
	if (t32_decode16(hw1) != T32_16_WIDE) {
		*insn = hw1;
		*length = 2U;
		return STD_E_OK;
	}
	if (bus_get_data16(core->core_id, address + 2U, &hw2) != STD_E_OK) {
		return STD_E_SEGV;
	}
	*insn = ((uint32)hw1 << 16) | hw2;
	*length = 4U;
	return STD_E_OK;
}

/*
 * The decode cache: the executor of the instruction at a PC in memory (ROM
 * or RAM of memory.txt), so that the common case reads the instruction's
 * halfwords straight from the host memory (memory_window.h), compares them
 * with the cached ones (code may change in RAM) and calls the executor
 * without the bus or the decoders. Direct-mapped by PC.
 */
#define DECODE_CACHE_BITS	20U
#define DECODE_CACHE_SIZE	(1U << DECODE_CACHE_BITS)

typedef struct {
	uint32 pc;		/* odd: empty */
	uint32 insn;		/* as fetch() returns it */
	uint16 length;
	std_bool rom;		/* fetched from ROM, which the CPU cannot write: not checked again */
	std_bool branch_to_self;
	ThumbExec16 exec16;
	ThumbExec32 exec32;
#ifdef ATHRILL_ENABLE_JIT
	/* The JIT code of the block that starts here, valid in this epoch (thumb_jit_run()). */
	const void *jit_code;
	uint32 jit_epoch;
	uint32 jit_length;
	uint32 jit_failed_epoch;	/* translation failed in this epoch: not tried again */
#endif
} ThumbDecodedType;

static ThumbDecodedType decode_cache[DECODE_CACHE_SIZE];
static std_bool decode_cache_ready = FALSE;

/* B/B<c> whose encoded target is its own address. */
static std_bool branch_to_self(uint32 insn, uint32 length)
{
	if (length == 2U) {
		return (insn == 0xE7FEU) /* B T2 */
			|| (((insn & 0xF0FFU) == 0xD0FEU)
				&& ((insn & 0x0E00U) != 0x0E00U)); /* B<c> T1 */
	}
	return (insn == 0xF7FFBFFEU) /* B T4 */
		|| (((insn & 0xFC3FFFFFU) == 0xF43FAFFEU)
			&& ((insn & 0x03800000U) != 0x03800000U)); /* B<c> T3 */
}

static uint16 halfword(const uint8 *p)
{
	return (uint16)(p[0] | ((uint16)p[1] << 8));
}

#if defined(__GNUC__) || defined(__clang__)
#define COLD_PATH	__attribute__((noinline, cold))
#else
#define COLD_PATH
#endif

/* The executors of an instruction without one: UNDEFINED. */
static Std_ReturnType undefined16(TargetCoreType *core, uint16 insn)
{
	(void)core;
	(void)insn;
	return STD_E_DECODE;
}

static Std_ReturnType undefined32(TargetCoreType *core, uint32 insn)
{
	(void)core;
	(void)insn;
	return STD_E_DECODE;
}

static Std_ReturnType guest_execute16(TargetCoreType *core, uint16 insn)
{
	ThumbExec16 exec = stm32_exec_fast ? thumb_fast_executor16(insn) : NULL;

	if (exec == NULL) {
		exec = thumb_executor16(insn);
	}
	return (exec != NULL) ? exec(core, insn) : undefined16(core, insn);
}

static Std_ReturnType guest_execute32(TargetCoreType *core, uint32 insn)
{
	ThumbExec32 exec = stm32_exec_fast ? thumb_fast_executor32(insn) : NULL;

	if (exec == NULL) {
		exec = thumb_executor32(insn);
	}
	return (exec != NULL) ? exec(core, insn) : undefined32(core, insn);
}

static Std_ReturnType host_execute16(TargetCoreType *core, uint16 insn)
{
	int id = athrill_host_function_find(core->reg.pc);

	if ((id >= 0) && athrill_host_function_invoke(core->core_id, id)) {
		return STD_E_OK;
	}
	return guest_execute16(core, insn);
}

static Std_ReturnType host_execute32(TargetCoreType *core, uint32 insn)
{
	int id = athrill_host_function_find(core->reg.pc);

	if ((id >= 0) && athrill_host_function_invoke(core->core_id, id)) {
		return STD_E_OK;
	}
	return guest_execute32(core, insn);
}

/* A miss: the instruction at pc decoded into its cache entry; NULL when pc is not in memory. */
COLD_PATH static const ThumbDecodedType *decode_into(ThumbDecodedType *e, uint32 pc)
{
	const uint8 *p = memory_window_code_pointer(pc, 2U);
	const uint8 *p2;
	uint16 hw1;

	if (p == NULL) {
		return NULL;
	}
	if (!decode_cache_ready) {
		memset(decode_cache, 0xFF, sizeof(decode_cache));
		decode_cache_ready = TRUE;
	}
	hw1 = halfword(p);
#ifdef ATHRILL_ENABLE_JIT
	e->jit_code = NULL;
	e->jit_failed_epoch = 0U;
#endif
	e->rom = memory_window_code_is_rom();
	if (t32_decode16(hw1) != T32_16_WIDE) {
		e->pc = pc;
		e->insn = hw1;
		e->length = 2U;
		e->branch_to_self = branch_to_self(e->insn, e->length);
		e->exec16 = stm32_exec_fast ? thumb_fast_executor16(hw1) : NULL;
		if (e->exec16 == NULL) {
			e->exec16 = thumb_executor16(hw1);
		}
		if (e->exec16 == NULL) {
			e->exec16 = undefined16;
		}
		if (athrill_host_function_find(pc) >= 0) {
			e->exec16 = host_execute16;
		}
		e->exec32 = NULL;
		return e;
	}
	p2 = memory_window_code_pointer(pc + 2U, 2U);
	if (p2 == NULL) {
		return NULL;
	}
	e->rom = e->rom && memory_window_code_is_rom();
	e->pc = pc;
	e->insn = ((uint32)hw1 << 16) | halfword(p2);
	e->length = 4U;
	e->branch_to_self = branch_to_self(e->insn, e->length);
	e->exec16 = NULL;
	e->exec32 = stm32_exec_fast ? thumb_fast_executor32(e->insn) : NULL;
	if (e->exec32 == NULL) {
		e->exec32 = thumb_executor32(e->insn);
	}
	if (e->exec32 == NULL) {
		e->exec32 = undefined32;
	}
	if (athrill_host_function_find(pc) >= 0) {
		e->exec32 = host_execute32;
	}
	return e;
}

#ifdef ATHRILL_ENABLE_JIT
static void thumb_jit_code_changed(void);
#endif

void thumb_invalidate_code(AthrillAddrType address, uint32 size)
{
	uint64 first;
	uint64 end;
	uint64 pc;

	if (!decode_cache_ready || (size == 0U)) {
		return;
	}
#ifdef ATHRILL_ENABLE_JIT
	thumb_jit_code_changed();
#endif
	first = (address >= 2U) ? ((uint64)address - 2U) : 0U;
	first &= ~1ULL;
	end = (uint64)address + size;
	for (pc = first; (pc < end) && (pc <= UINT32_MAX); pc += 2U) {
		ThumbDecodedType *entry = &decode_cache[((uint32)pc >> 1) & (DECODE_CACHE_SIZE - 1U)];
		if (entry->pc == (uint32)pc) {
			entry->pc = 1U;
		}
	}
}

/* The cached instruction at pc, decoded on a miss; NULL when pc is not in memory. */
static inline const ThumbDecodedType *decoded_at(uint32 pc)
{
	ThumbDecodedType *e = &decode_cache[(pc >> 1) & (DECODE_CACHE_SIZE - 1U)];
	const uint8 *p;

	if (e->pc == pc) {
		if (e->rom) {
			return e;
		}
		p = memory_window_code_pointer(pc, e->length);
		if ((p != NULL) && ((e->length == 2U) ? (e->insn == halfword(p))
			: (e->insn == (((uint32)halfword(p) << 16) | halfword(p + 2U))))) {
			return e;
		}
	}
	return decode_into(e, pc);
}

std_bool thumb_is_branch_to_self(CoreIdType core_id, AthrillAddrType pc)
{
	const ThumbDecodedType *decoded;

	(void)core_id;
	decoded = decoded_at((uint32)pc);
	return (decoded != NULL) && decoded->branch_to_self;
}

/* ExecuteFPCheck() (arm_profile.h): the lazy floating-point context of the M profile. */
Std_ReturnType ArmExecuteFPCheck(TargetCoreType *core)
{
	return ArmMExecuteFPCheck(core);
}

/*
 * Ends a step that takes an exception: the next instruction is its handler.
 */
static Std_ReturnType take(TargetCoreType *core, Std_ReturnType err)
{
	if (err != STD_E_OK) {
		return err;
	}
	core->reg.pc = core->reg.next_pc;
	return STD_E_OK;
}

/*
 * After an instruction: what it asked for (an exception return, an EXC_RETURN
 * value written to PC in Handler mode, or a synchronous exception: SVCall, a
 * fault), in that order, after the IT state has advanced; or its error.
 */
COLD_PATH static Std_ReturnType complete(TargetCoreType *core, Std_ReturnType err)
{
	if (err == STD_E_DECODE) {
		/* UNDEFINED (no executor, or an UNDEFINED case of an encoding): UsageFault UNDEFINSTR */
		core->m.cfsr |= ARM_M_UFSR_UNDEFINSTR << 16;
		return take(core, ArmMTakeSynchronous(core, ARM_M_EXC_USAGEFAULT, core->reg.pc));
	}
	if (err == STD_E_SEGV) {
		core->access_fault = TRUE;
	}
	if (err != STD_E_OK) {
		return err;
	}
	/* ITAdvance() after every instruction but IT, whether its condition
	 * passed or not. */
	if (!core->it_written) {
		core->reg.itstate = ArmITAdvance(core->reg.itstate);
	}
	if (core->exc_return_pending) {
		err = ArmMExceptionReturn(core, core->exc_return);
		if (err != STD_E_OK) {
			return err;
		}
	}
	if (core->sync_exception != 0U) {
		return take(core, ArmMTakeSynchronous(core, core->sync_exception, core->sync_return));
	}
	core->reg.pc = core->reg.next_pc;
	return STD_E_OK;
}

/*
 * A step that may take an exception first (the highest-priority pending
 * exception that preempts, or the UsageFault of EPSR.T clear), or whose
 * instruction is not in memory (fetched through the bus).
 */
COLD_PATH static Std_ReturnType execute_slow(TargetCoreType *core)
{
	uint32 insn;
	uint32 length;
	uint32 n;
	Std_ReturnType err;
	const ThumbDecodedType *decoded;

	if (core->m.lockup) {
		return STD_E_EXEC;
	}
	n = ArmMPendingToTakeFast(&core->m);
	if (n != 0U) {
		return take(core, ArmMExceptionEntry(core, n, core->reg.pc));
	}
	if (!core->m.epsr_t) {
		core->m.cfsr |= ARM_M_UFSR_INVSTATE << 16;
		return take(core, ArmMTakeSynchronous(core, ARM_M_EXC_USAGEFAULT, core->reg.pc));
	}
	cpu_trace_pc(core->reg.pc);
	core->it_written = FALSE;
	core->sync_exception = 0U;
	core->exc_return_pending = FALSE;
	decoded = decoded_at(core->reg.pc);
	if (decoded != NULL) {
		core->reg.next_pc = core->reg.pc + decoded->length;
		if (decoded->length == 2U) {
			err = decoded->exec16(core, (uint16)decoded->insn);
		}
		else {
			err = decoded->exec32(core, decoded->insn);
		}
		return complete(core, err);
	}
	err = fetch(core, core->reg.pc, &insn, &length);
	if (err != STD_E_OK) {
		core->access_fault = TRUE;
		return err;
	}
	core->reg.next_pc = core->reg.pc + length;
	if (length == 2U) {
		err = thumb_execute16(core, (uint16)insn);
	}
	else {
		err = thumb_execute32(core, insn);
	}
	return complete(core, err);
}

/*
 * One step. The common case, kept short: no exception to look at, the
 * instruction in the decode cache, nothing asked for after it.
 */
static inline Std_ReturnType execute_one(TargetCoreType *core)
{
	const ThumbDecodedType *decoded;
	Std_ReturnType err;
	uint32 pc = core->reg.pc;
	std_bool in_it = core->reg.itstate != 0U;

	if (core->m.pending_check || core->m.priority_check || core->m.lockup || !core->m.epsr_t) {
		return execute_slow(core);
	}
	decoded = decoded_at(core->reg.pc);
	if (decoded == NULL) {
		return execute_slow(core);
	}
	cpu_trace_pc(core->reg.pc);
	core->reg.next_pc = core->reg.pc + decoded->length;
	core->it_written = FALSE;
	core->sync_exception = 0U;
	core->exc_return_pending = FALSE;
	if (decoded->length == 2U) {
		err = decoded->exec16(core, (uint16)decoded->insn);
	}
	else {
		err = decoded->exec32(core, decoded->insn);
	}
	if ((err != STD_E_OK) || core->exc_return_pending || (core->sync_exception != 0U)) {
		return complete(core, err);
	}
	if ((core->reg.itstate != 0U) && !core->it_written) {
		core->reg.itstate = ArmITAdvance(core->reg.itstate);
	}
	core->reg.pc = core->reg.next_pc;
	if ((core->reg.pc == pc) && decoded->branch_to_self && !in_it) {
		athrill_idle_spin_halt_after_execute(core->core_id, pc);
	}
	return STD_E_OK;
}

Std_ReturnType thumb_execute(TargetCoreType *core)
{
	return execute_one(core);
}

#ifdef ATHRILL_ENABLE_JIT
/*
 * JIT of straight-line blocks (experiment, DEVICE_CONFIG_STM32_JIT 1).
 *
 * A block: the cached instructions of ROM from a PC on, each the one after
 * the other, up to THUMB_JIT_MAX_LENGTH, ending before an instruction that
 * is not cached, not in ROM, a host function or at the idle PC, and after an
 * IT. A block runs only with ITSTATE 0 and nothing for execute_slow() to do
 * (no exception to look at, no lockup, EPSR.T set), so no instruction of it
 * is in an IT block and the IT state stays 0 until its IT.
 *
 * Every instruction is called (its executor with its encoding, directly),
 * as execute_one() runs it: the clock counted before it, PC, next_pc and the
 * per-instruction fields set, then the executor. After it, the block ends
 * as execute_one() and thumb_execute_run() would stop or go elsewhere:
 *   - an error, an exception return or a synchronous exception: to C, which
 *     runs complete() (exit THUMB_JIT_EXIT_COMPLETE);
 *   - next_pc not the next instruction: PC = next_pc; to itself: to C for the
 *     spin-halt check (THUMB_JIT_EXIT_SELF); elsewhere through the core's
 *     indirect table (another block straight on) or back to C;
 *   - a device accessed, a halt, a reset, something for execute_slow():
 *     back to C before the next instruction.
 * The end of a block is a chain exit to the next PC (not after an IT: the
 * next block would start inside an IT block, and not to the idle PC).
 */
#include "athrill_jit.h"
#include "cpuemu_ops.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#define THUMB_JIT_MAX_LENGTH 32U

enum {
	THUMB_JIT_EXIT_NONE = 0,
	THUMB_JIT_EXIT_COMPLETE,
	THUMB_JIT_EXIT_SELF
};

typedef struct {
	uint64 clock_base;	/* the clock when the run started: the clock is clock_base + done */
	uint64 budget;		/* clocks this run may use: a block's entry check */
	uint64 chain_slot;	/* the unlinked chain exit the run ended at (0: none) */
	uint64 indirect_miss;	/* 1: ended at a branch whose target was not in the table */
	uint32 exit_kind;
	uint32 err;
	uint32 fx;	/* flag computations: the operands, the result, a scratch value */
	uint32 fy;
	uint32 fr;
	uint32 ft;
} ThumbJitCtx;

static int thumb_jit_mode = -1;	/* -1: not read yet, 0: off, 1: on */
static AthrillJitCache *thumb_jit_cache;
static uint64 *thumb_jit_clock;
static uint32 thumb_jit_epoch = 1U;
static AthrillJitChainSlot *thumb_jit_pending_slot;
static std_bool thumb_jit_pending_indirect;
static ThumbJitCtx thumb_jit_ctx;
static uint64 thumb_jit_runs;
static uint64 thumb_jit_insns;
static uint64 thumb_jit_translated;
static uint64 thumb_jit_failures;
#ifdef ATHRILL_STM32_JIT_DIFF_TEST
static ThumbJitDiffStats thumb_jit_diff_stats;
#endif

#define TJ_CORE(field, width) athrill_jit_core((uint32)offsetof(TargetCoreType, field), width)
#define TJ_CTX(field, width) athrill_jit_ctx((uint32)offsetof(ThumbJitCtx, field), width)
#define TJ_T0 athrill_jit_temp(0U, 8)
#define TJ_T1 athrill_jit_temp(1U, 8)

static void thumb_jit_report(void)
{
	AthrillJitCacheStats st;

	athrill_jit_cache_get_stats(thumb_jit_cache, &st);
	fprintf(stderr, "INFO: JIT block runs from C=%" PRIu64 " insns=%" PRIu64 " translated=%" PRIu64
		" failed=%" PRIu64 " held=%u code=%" PRIu64 " bytes links=%u\n", thumb_jit_runs, thumb_jit_insns,
		thumb_jit_translated, thumb_jit_failures, st.blocks, st.code_bytes, athrill_jit_chain_links());
}

static std_bool thumb_jit_enabled(void)
{
	uint32 value = 0U;

	if (thumb_jit_mode >= 0) {
		return thumb_jit_mode == 1;
	}
	thumb_jit_mode = 0;
	if ((cpuemu_get_devcfg_value("DEVICE_CONFIG_STM32_JIT", &value) == STD_E_OK) && (value == 1U)) {
		thumb_jit_cache = athrill_jit_cache_create(65536U);
		if (thumb_jit_cache != NULL) {
			thumb_jit_mode = 1;
			fprintf(stderr, "INFO: STM32 JIT on (experiment)\n");
			(void)atexit(thumb_jit_report);
		}
	}
	return thumb_jit_mode == 1;
}

#ifdef ATHRILL_STM32_JIT_DIFF_TEST
void thumb_jit_diff_reset(void)
{
	athrill_jit_chain_unlink_all();
	athrill_jit_cache_destroy(thumb_jit_cache);
	thumb_jit_cache = NULL;
	thumb_jit_mode = 0;
	thumb_jit_clock = NULL;
	thumb_jit_pending_slot = NULL;
	thumb_jit_pending_indirect = FALSE;
	thumb_jit_epoch++;
	thumb_jit_runs = 0U;
	thumb_jit_insns = 0U;
	thumb_jit_translated = 0U;
	memset(&thumb_jit_diff_stats, 0, sizeof(thumb_jit_diff_stats));
}

void thumb_jit_diff_set_enabled(std_bool enabled)
{
	if (enabled && (thumb_jit_cache == NULL)) {
		thumb_jit_cache = athrill_jit_cache_create(256U);
	}
	thumb_jit_mode = (enabled && (thumb_jit_cache != NULL)) ? 1 : 0;
}

void thumb_jit_diff_get_stats(ThumbJitDiffStats *stats)
{
	*stats = thumb_jit_diff_stats;
	stats->runs = thumb_jit_runs;
	stats->insns = thumb_jit_insns;
	stats->translated = thumb_jit_translated;
	stats->chain_links = athrill_jit_chain_links();
}

static void thumb_jit_diff_count(AthrillJitBuilder *b, uint64 *counter)
{
	athrill_jit_alu(b, ATHRILL_JIT_ADD, athrill_jit_abs(counter, 8),
		athrill_jit_abs(counter, 8), athrill_jit_imm(1));
}
#define THUMB_JIT_DIFF_COUNT(b, n) thumb_jit_diff_count((b), &thumb_jit_diff_stats.native[(n)])
#else
#define THUMB_JIT_DIFF_COUNT(b, n) ((void)0)
#endif

/* Code changed: no compiled block, link or table entry stays. */
static void thumb_jit_code_changed(void)
{
	athrill_jit_chain_unlink_all();
	thumb_jit_pending_slot = NULL;
	thumb_jit_pending_indirect = FALSE;
	thumb_jit_epoch++;
}

static std_bool thumb_jit_is_it(const ThumbDecodedType *e)
{
	return (e->length == 2U) && ((e->insn & 0xFF00U) == 0xBF00U) && ((e->insn & 0x000FU) != 0U);
}

static std_bool thumb_jit_is_host(const ThumbDecodedType *e)
{
	return (e->exec16 == host_execute16) || (e->exec32 == host_execute32);
}

/* The cached ROM entry for pc, or NULL (no check of the bytes needed: ROM). */
static inline ThumbDecodedType *thumb_jit_entry(uint32 pc)
{
	ThumbDecodedType *e = &decode_cache[(pc >> 1) & (DECODE_CACHE_SIZE - 1U)];

	if ((e->pc != pc) || !e->rom) {
		return NULL;
	}
	return e;
}

/* An exit to a known PC (PC written): straight on to its block when linked. */
static void thumb_jit_exit_to(AthrillJitBuilder *b, uint32 pc)
{
	AthrillAddrType idle_pc = athrill_idle_spin_halt_idle_pc();

	if ((AthrillAddrType)pc == idle_pc) {
		athrill_jit_exit(b);
		return;
	}
	athrill_jit_chain_exit(b, TJ_CTX(chain_slot, 8), pc);
}

/* exit_if(field != 0) for a 4-byte flag of the core */
static void thumb_jit_exit_if_set(AthrillJitBuilder *b, AthrillJitOperand flag)
{
	athrill_jit_exit_if(b, ATHRILL_JIT_NE, flag, athrill_jit_imm(0));
}

/*
 * What an executor may touch, from the decoder's name of its encoding:
 *   PURE: registers and APSR only (data processing, ADR, B, BL, CBZ/CBNZ);
 *     it can only fail (an UNPREDICTABLE case: its result) or branch
 *     (next_pc), nothing else needs a check after it;
 *   MEMORY: loads, stores, the floating-point instructions (memory, the lazy
 *     FP context): a device, the NVIC/SCS, a fault, an exception return;
 *   OTHER: everything else (system, hints, BX/BLX, exceptions): every check.
 */
enum {
	THUMB_JIT_PURE = 0,
	THUMB_JIT_MEMORY,
	THUMB_JIT_OTHER
};

static std_bool thumb_jit_name_is(const char *name, const char *const *prefixes)
{
	uint32 i;

	for (i = 0U; prefixes[i] != NULL; i++) {
		if (strncmp(name, prefixes[i], strlen(prefixes[i])) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

static uint32 thumb_jit_kind(const ThumbDecodedType *e)
{
	static const char *const pure[] = {
		"ADD_", "ADC_", "SUB_", "SBC_", "RSB_", "MOV_", "MVN_", "CMP_", "CMN_", "AND_", "ORR_", "ORN_", "EOR_",
		"BIC_", "TST_", "TEQ_", "MUL_", "MLA_", "MLS_", "UMULL", "SMULL", "UMLAL", "SMLAL", "SXT", "UXT", "REV",
		"RBIT", "CLZ", "BFI", "BFC", "UBFX", "SBFX", "ADR_", "LSL", "LSR", "ASR", "ROR", "UDIV", "SDIV",
		"B_T", "BL_", "CBZ", "CBNZ", "NOP", NULL
	};
	static const char *const memory[] = {
		"LDR", "STR", "PUSH", "POP", "LDM", "STM", "TBB", "TBH", "V", "PLD", "PLI", NULL
	};
	const char *name = (e->length == 2U) ? t32_decode16_names[t32_decode16((uint16)e->insn)]
		: t32_decode32_names[t32_decode32(e->insn)];

	if (thumb_jit_name_is(name, pure)) {
		return THUMB_JIT_PURE;
	}
	if (thumb_jit_name_is(name, memory)) {
		return THUMB_JIT_MEMORY;
	}
	return THUMB_JIT_OTHER;
}

/*
 * Native instructions. A block runs outside IT blocks (ITSTATE 0), so each
 * of these is what its executor does there: the condition is AL, the
 * 16-bit data-processing forms set the flags, InITBlock() is FALSE.
 */
#define TJ_R(n) TJ_CORE(reg.r[n], 4)
#define TJ_APSR TJ_CORE(reg.apsr, 4)
#define TJ_T0W athrill_jit_temp(0U, 4)
#define TJ_T1W athrill_jit_temp(1U, 4)
#define TJ_NZCV (ARM_APSR_N | ARM_APSR_Z | ARM_APSR_C | ARM_APSR_V)

typedef struct {
	AthrillJitLabel *label;
	uint32 target;
} ThumbJitTaken;

/*
 * R[d] (d < 0: none) = AddWithCarry(x, y, 0) or (x, NOT(y), 1), with APSR.NZCV:
 * N the result's bit 31, Z result == 0, C the carry out (add: result < x;
 * subtract: x >= y, no borrow), V the signed overflow (add: (x^r)&(y^r);
 * subtract: (x^y)&(x^r), bit 31). Q and GE are kept.
 */
static void thumb_jit_add_sub(AthrillJitBuilder *b, sint32 d, AthrillJitOperand x, AthrillJitOperand y, std_bool sub)
{
	athrill_jit_mov(b, TJ_CTX(fx, 4), x);
	athrill_jit_mov(b, TJ_CTX(fy, 4), y);
	athrill_jit_alu(b, sub ? ATHRILL_JIT_SUB : ATHRILL_JIT_ADD, TJ_CTX(fr, 4), TJ_CTX(fx, 4), TJ_CTX(fy, 4));
	if (d >= 0) {
		athrill_jit_mov(b, TJ_R(d), TJ_CTX(fr, 4));
	}
	athrill_jit_alu(b, ATHRILL_JIT_AND, TJ_T0W, TJ_CTX(fr, 4), athrill_jit_imm((sint64)ARM_APSR_N));
	athrill_jit_set_if(b, ATHRILL_JIT_EQ, TJ_T1W, TJ_CTX(fr, 4), athrill_jit_imm(0));
	athrill_jit_alu(b, ATHRILL_JIT_SHL, TJ_T1W, TJ_T1W, athrill_jit_imm(ARM_APSR_BITPOS_Z));
	athrill_jit_alu(b, ATHRILL_JIT_OR, TJ_T0W, TJ_T0W, TJ_T1W);
	if (sub) {
		athrill_jit_set_if(b, ATHRILL_JIT_GEU, TJ_T1W, TJ_CTX(fx, 4), TJ_CTX(fy, 4));
	} else {
		athrill_jit_set_if(b, ATHRILL_JIT_LTU, TJ_T1W, TJ_CTX(fr, 4), TJ_CTX(fx, 4));
	}
	athrill_jit_alu(b, ATHRILL_JIT_SHL, TJ_T1W, TJ_T1W, athrill_jit_imm(ARM_APSR_BITPOS_C));
	athrill_jit_alu(b, ATHRILL_JIT_OR, TJ_T0W, TJ_T0W, TJ_T1W);
	if (sub) {
		athrill_jit_alu(b, ATHRILL_JIT_XOR, TJ_T1W, TJ_CTX(fx, 4), TJ_CTX(fy, 4));
		athrill_jit_alu(b, ATHRILL_JIT_XOR, TJ_CTX(ft, 4), TJ_CTX(fx, 4), TJ_CTX(fr, 4));
	} else {
		athrill_jit_alu(b, ATHRILL_JIT_XOR, TJ_T1W, TJ_CTX(fx, 4), TJ_CTX(fr, 4));
		athrill_jit_alu(b, ATHRILL_JIT_XOR, TJ_CTX(ft, 4), TJ_CTX(fy, 4), TJ_CTX(fr, 4));
	}
	athrill_jit_alu(b, ATHRILL_JIT_AND, TJ_T1W, TJ_T1W, TJ_CTX(ft, 4));
	athrill_jit_alu(b, ATHRILL_JIT_SHR, TJ_T1W, TJ_T1W, athrill_jit_imm(31));
	athrill_jit_alu(b, ATHRILL_JIT_SHL, TJ_T1W, TJ_T1W, athrill_jit_imm(ARM_APSR_BITPOS_V));
	athrill_jit_alu(b, ATHRILL_JIT_OR, TJ_T0W, TJ_T0W, TJ_T1W);
	athrill_jit_alu(b, ATHRILL_JIT_AND, TJ_APSR, TJ_APSR, athrill_jit_imm((sint64)(~TJ_NZCV & 0xFFFFFFFFU)));
	athrill_jit_alu(b, ATHRILL_JIT_OR, TJ_APSR, TJ_APSR, TJ_T0W);
}

/* The NZCV values (bit n = nzcv value n) for which ConditionHolds(cond). */
static uint32 thumb_jit_cond_mask(uint32 cond)
{
	uint32 nzcv;
	uint32 mask = 0U;

	for (nzcv = 0U; nzcv < 16U; nzcv++) {
		if (ConditionHolds(cond, nzcv << 28)) {
			mask |= 1U << nzcv;
		}
	}
	return mask;
}

/* A word of ROM at address, for a literal load (FALSE: not ROM). */
static std_bool thumb_jit_rom_word(uint32 address, uint32 *value)
{
	const uint8 *p = memory_window_code_pointer(address, 4U);

	if ((p == NULL) || !memory_window_code_is_rom()) {
		return FALSE;
	}
	memcpy(value, p, 4U);
	return TRUE;
}

/*
 * Emits e natively; FALSE: not one of these (it is called). *ends: the block
 * ends after it (an unconditional branch, written as a chain exit to its
 * target). A conditional branch adds its taken exit to *taken.
 */
static std_bool thumb_jit_native(AthrillJitBuilder *b, const ThumbDecodedType *e, std_bool *ends,
				 ThumbJitTaken *taken, uint32 *taken_count)
{
	uint32 pc = e->pc;
	uint32 pc4 = pc + 4U;	/* ArmPC() */
	uint32 target;
	uint32 value;
	uint32 d;
	uint32 m;
	uint16 insn;

	*ends = FALSE;
	if (e->length == 4U) {
		if (t32_decode32(e->insn) != T32_32_BL_i_T1) {
			return FALSE;
		}
		{
			uint32 sbit = t32w_BL_i_T1_S(e->insn);
			uint32 i1 = (t32w_BL_i_T1_J1(e->insn) ^ sbit) ^ 1U;
			uint32 i2 = (t32w_BL_i_T1_J2(e->insn) ^ sbit) ^ 1U;
			uint32 imm32 = SignExtend((sbit << 24) | (i1 << 23) | (i2 << 22) | (t32w_BL_i_T1_imm10(e->insn) << 12)
				| (t32w_BL_i_T1_imm11(e->insn) << 1), 25U);

			target = (pc4 + imm32) & ~1U;
		}
		THUMB_JIT_DIFF_COUNT(b, THUMB_JIT_DIFF_BL);
		athrill_jit_mov(b, TJ_R(14U), athrill_jit_imm((sint64)(pc4 | 1U)));	/* LR */
		athrill_jit_mov(b, TJ_CORE(reg.pc, 4), athrill_jit_imm((sint64)target));
		athrill_jit_mov(b, TJ_CORE(reg.next_pc, 4), athrill_jit_imm((sint64)target));
		thumb_jit_exit_to(b, target);
		*ends = TRUE;
		return TRUE;
	}
	insn = (uint16)e->insn;
	switch (t32_decode16(insn)) {
	case T32_16_MOV_i_T1:
		THUMB_JIT_DIFF_COUNT(b, THUMB_JIT_DIFF_MOV_I);
		value = t32_MOV_i_T1_imm8(insn);
		athrill_jit_mov(b, TJ_R(t32_MOV_i_T1_Rd(insn)), athrill_jit_imm((sint64)value));
		athrill_jit_alu(b, ATHRILL_JIT_AND, TJ_APSR, TJ_APSR,
				athrill_jit_imm((sint64)(~(ARM_APSR_N | ARM_APSR_Z) & 0xFFFFFFFFU)));
		if (value == 0U) {
			athrill_jit_alu(b, ATHRILL_JIT_OR, TJ_APSR, TJ_APSR, athrill_jit_imm((sint64)ARM_APSR_Z));
		}
		return TRUE;
	case T32_16_MOV_r_T1:
		d = (t32_MOV_r_T1_D(insn) << 3) | t32_MOV_r_T1_Rd(insn);
		m = t32_MOV_r_T1_Rm(insn);
		if (d == ARM_REG_PC) {
			return FALSE;	/* ALUWritePC */
		}
		THUMB_JIT_DIFF_COUNT(b, THUMB_JIT_DIFF_MOV_R);
		if (m == ARM_REG_PC) {
			athrill_jit_mov(b, TJ_R(d), athrill_jit_imm((sint64)pc4));
		} else {
			athrill_jit_mov(b, TJ_R(d), TJ_R(m));
		}
		return TRUE;
	case T32_16_CMP_i_T1:
		THUMB_JIT_DIFF_COUNT(b, THUMB_JIT_DIFF_CMP_I);
		thumb_jit_add_sub(b, -1, TJ_R(t32_CMP_i_T1_Rn(insn)), athrill_jit_imm(t32_CMP_i_T1_imm8(insn)), TRUE);
		return TRUE;
	case T32_16_CMP_r_T1:
		THUMB_JIT_DIFF_COUNT(b, THUMB_JIT_DIFF_CMP_R);
		thumb_jit_add_sub(b, -1, TJ_R(t32_CMP_r_T1_Rn(insn)), TJ_R(t32_CMP_r_T1_Rm(insn)), TRUE);
		return TRUE;
	case T32_16_ADD_i_T1:
		THUMB_JIT_DIFF_COUNT(b, THUMB_JIT_DIFF_ADD_I3);
		thumb_jit_add_sub(b, (sint32)t32_ADD_i_T1_Rd(insn), TJ_R(t32_ADD_i_T1_Rn(insn)),
				  athrill_jit_imm(t32_ADD_i_T1_imm3(insn)), FALSE);
		return TRUE;
	case T32_16_SUB_i_T1:
		THUMB_JIT_DIFF_COUNT(b, THUMB_JIT_DIFF_SUB_I3);
		thumb_jit_add_sub(b, (sint32)t32_SUB_i_T1_Rd(insn), TJ_R(t32_SUB_i_T1_Rn(insn)),
				  athrill_jit_imm(t32_SUB_i_T1_imm3(insn)), TRUE);
		return TRUE;
	case T32_16_ADD_i_T2:
		THUMB_JIT_DIFF_COUNT(b, THUMB_JIT_DIFF_ADD_I8);
		d = t32_ADD_i_T2_Rdn(insn);
		thumb_jit_add_sub(b, (sint32)d, TJ_R(d), athrill_jit_imm(t32_ADD_i_T2_imm8(insn)), FALSE);
		return TRUE;
	case T32_16_SUB_i_T2:
		THUMB_JIT_DIFF_COUNT(b, THUMB_JIT_DIFF_SUB_I8);
		d = t32_SUB_i_T2_Rdn(insn);
		thumb_jit_add_sub(b, (sint32)d, TJ_R(d), athrill_jit_imm(t32_SUB_i_T2_imm8(insn)), TRUE);
		return TRUE;
	case T32_16_ADD_r_T1:
		THUMB_JIT_DIFF_COUNT(b, THUMB_JIT_DIFF_ADD_R);
		thumb_jit_add_sub(b, (sint32)t32_ADD_r_T1_Rd(insn), TJ_R(t32_ADD_r_T1_Rn(insn)),
				  TJ_R(t32_ADD_r_T1_Rm(insn)), FALSE);
		return TRUE;
	case T32_16_SUB_r_T1:
		THUMB_JIT_DIFF_COUNT(b, THUMB_JIT_DIFF_SUB_R);
		thumb_jit_add_sub(b, (sint32)t32_SUB_r_T1_Rd(insn), TJ_R(t32_SUB_r_T1_Rn(insn)),
				  TJ_R(t32_SUB_r_T1_Rm(insn)), TRUE);
		return TRUE;
	case T32_16_LDR_l_T1:
		if (!thumb_jit_rom_word((pc4 & ~3U) + (t32_LDR_l_T1_imm8(insn) << 2), &value)) {
			return FALSE;
		}
		THUMB_JIT_DIFF_COUNT(b, THUMB_JIT_DIFF_LDR_LITERAL);
		athrill_jit_mov(b, TJ_R(t32_LDR_l_T1_Rt(insn)), athrill_jit_imm((sint64)value));
		return TRUE;
	case T32_16_B_T1:
		target = (pc4 + SignExtend(t32_B_T1_imm8(insn) << 1, 9U)) & ~1U;
		if ((target == pc) || (*taken_count >= THUMB_JIT_MAX_LENGTH)) {
			return FALSE;	/* to itself: the spin-halt check in C */
		}
		THUMB_JIT_DIFF_COUNT(b, THUMB_JIT_DIFF_B_COND);
		athrill_jit_alu(b, ATHRILL_JIT_SHR, TJ_T0W, TJ_APSR, athrill_jit_imm(28));
		athrill_jit_mov(b, TJ_T1W, athrill_jit_imm((sint64)thumb_jit_cond_mask(t32_B_T1_cond(insn))));
		athrill_jit_alu(b, ATHRILL_JIT_SHR, TJ_T1W, TJ_T1W, TJ_T0W);
		athrill_jit_alu(b, ATHRILL_JIT_AND, TJ_T1W, TJ_T1W, athrill_jit_imm(1));
		taken[*taken_count].label = athrill_jit_label_new(b);
		taken[*taken_count].target = target;
		athrill_jit_jump_if(b, ATHRILL_JIT_NE, TJ_T1W, athrill_jit_imm(0), taken[*taken_count].label);
		(*taken_count)++;
		return TRUE;
	case T32_16_CBZ_T1:
	case T32_16_CBNZ_T1:
		if (*taken_count >= THUMB_JIT_MAX_LENGTH) {
			return FALSE;
		}
		THUMB_JIT_DIFF_COUNT(b, (t32_decode16(insn) == T32_16_CBZ_T1)
			? THUMB_JIT_DIFF_CBZ : THUMB_JIT_DIFF_CBNZ);
		if (t32_decode16(insn) == T32_16_CBZ_T1) {
			target = pc4 + ((t32_CBZ_T1_i(insn) << 6) | (t32_CBZ_T1_imm5(insn) << 1));
			m = t32_CBZ_T1_Rn(insn);
		} else {
			target = pc4 + ((t32_CBNZ_T1_i(insn) << 6) | (t32_CBNZ_T1_imm5(insn) << 1));
			m = t32_CBNZ_T1_Rn(insn);
		}
		taken[*taken_count].label = athrill_jit_label_new(b);
		taken[*taken_count].target = target & ~1U;
		athrill_jit_jump_if(b, (t32_decode16(insn) == T32_16_CBZ_T1) ? ATHRILL_JIT_EQ : ATHRILL_JIT_NE,
				    TJ_R(m), athrill_jit_imm(0), taken[*taken_count].label);
		(*taken_count)++;
		return TRUE;
	case T32_16_B_T2:
		target = (pc4 + SignExtend(t32_B_T2_imm11(insn) << 1, 12U)) & ~1U;
		if (target == pc) {
			return FALSE;
		}
		THUMB_JIT_DIFF_COUNT(b, THUMB_JIT_DIFF_B);
		athrill_jit_mov(b, TJ_CORE(reg.pc, 4), athrill_jit_imm((sint64)target));
		athrill_jit_mov(b, TJ_CORE(reg.next_pc, 4), athrill_jit_imm((sint64)target));
		thumb_jit_exit_to(b, target);
		*ends = TRUE;
		return TRUE;
	default:
		return FALSE;
	}
}

/* exit if C has something to do before the next instruction (after one of kind) */
static void thumb_jit_checks(AthrillJitBuilder *b, uint32 kind)
{
	if (kind == THUMB_JIT_PURE) {
		return;
	}
	thumb_jit_exit_if_set(b, athrill_jit_abs(&stm32_device_accessed, 4));
	thumb_jit_exit_if_set(b, TJ_CORE(m.pending_check, 4));
	thumb_jit_exit_if_set(b, TJ_CORE(m.priority_check, 4));
	if (kind == THUMB_JIT_MEMORY) {
		return;
	}
	thumb_jit_exit_if_set(b, TJ_CORE(is_halt, 4));
	thumb_jit_exit_if_set(b, TJ_CORE(reset_pending, 4));
	thumb_jit_exit_if_set(b, TJ_CORE(scs.reset_requested, 4));
	thumb_jit_exit_if_set(b, TJ_CORE(m.lockup, 4));
	athrill_jit_exit_if(b, ATHRILL_JIT_EQ, TJ_CORE(m.epsr_t, 4), athrill_jit_imm(0));
}

/*
 * The branches a block ends with (basic blocks: a block is then entered at
 * its start, not in its middle, and both ways out are chain exits):
 * B T1, B T2, CBZ, CBNZ, BL. thumb_jit_native() writes them as the exits.
 */
static std_bool thumb_jit_is_unconditional_branch(const ThumbDecodedType *e)
{
	T32Decode16Id id;

	if (e->length == 4U) {
		return t32_decode32(e->insn) == T32_32_BL_i_T1;
	}
	id = t32_decode16((uint16)e->insn);
	return (id == T32_16_B_T2) || (id == T32_16_B_T1) || (id == T32_16_CBZ_T1) || (id == T32_16_CBNZ_T1);
}

/*
 * A called instruction: its executor with its encoding, then what its kind
 * needs (complete(), a taken branch, the checks before the next one).
 */
static void thumb_jit_emit_call(AthrillJitBuilder *b, const ThumbDecodedType *e, uint32 kind,
				AthrillJitLabel *complete_exit, AthrillJitLabel *taken)
{
	uint32 next = e->pc + e->length;

	if (kind != THUMB_JIT_PURE) {
		/* the clock the instruction may read (a device): its own included */
		athrill_jit_alu(b, ATHRILL_JIT_ADD, athrill_jit_abs(thumb_jit_clock, 8), TJ_CTX(clock_base, 8),
				athrill_jit_done());
	}
	athrill_jit_mov(b, TJ_CORE(reg.pc, 4), athrill_jit_imm((sint64)e->pc));
	athrill_jit_mov(b, TJ_CORE(reg.next_pc, 4), athrill_jit_imm((sint64)next));
#ifdef ATHRILL_STM32_JIT_DIFF_TEST
	thumb_jit_diff_count(b, &thumb_jit_diff_stats.called);
#endif
	if (e->length == 2U) {
		athrill_jit_call_value(b, (AthrillJitCallValueFunc)(void *)e->exec16, e->insn);
	} else {
		athrill_jit_call_value(b, (AthrillJitCallValueFunc)(void *)e->exec32, e->insn);
	}
	athrill_jit_jump_if(b, ATHRILL_JIT_NE, athrill_jit_result(), athrill_jit_imm(0), complete_exit);
	if (kind != THUMB_JIT_PURE) {
		athrill_jit_jump_if(b, ATHRILL_JIT_NE, TJ_CORE(exc_return_pending, 4), athrill_jit_imm(0), complete_exit);
		athrill_jit_jump_if(b, ATHRILL_JIT_NE, TJ_CORE(sync_exception, 4), athrill_jit_imm(0), complete_exit);
	}
	athrill_jit_jump_if(b, ATHRILL_JIT_NE, TJ_CORE(reg.next_pc, 4), athrill_jit_imm((sint64)next), taken);
	athrill_jit_mov(b, TJ_CORE(reg.pc, 4), athrill_jit_imm((sint64)next));
	thumb_jit_checks(b, kind);
}

/*
 * Native loads and stores of plain memory: an access inside the RAM region
 * (or, for a load, the ROM region) is the host access the executor makes
 * through memory_window_pointer() / memory_window_write_pointer(); anything
 * else takes the C path: the instruction called, after the block's end
 * (ThumbJitSlow), back to resume.
 */
typedef struct {
	std_bool known;
	uint32 start;
	uint32 size;
	uint8 *data;
} ThumbJitRegion;

static ThumbJitRegion thumb_jit_ram;
static ThumbJitRegion thumb_jit_rom;

typedef struct {
	AthrillJitLabel *start;
	AthrillJitLabel *resume;
	const ThumbDecodedType *entry;
} ThumbJitSlow;

/* The plain memory region holding addr (memory_window_region(): the windows unchanged). */
static void thumb_jit_find_region(ThumbJitRegion *r, uint32 addr, std_bool writable)
{
	MemoryWindowType region;

	if (!memory_window_region(addr, &region) || (region.writable != writable) || (region.size < 4U)) {
		return;
	}
	r->start = region.start;
	r->size = region.size;
	r->data = region.data;
	r->known = TRUE;
}

/* T0 = (R[n] + imm) mod 2^32 */
static void thumb_jit_address(AthrillJitBuilder *b, uint32 n, uint32 imm)
{
	athrill_jit_mov(b, TJ_T0, TJ_R(n));
	athrill_jit_alu(b, ATHRILL_JIT_ADD, TJ_T0, TJ_T0, athrill_jit_imm((sint64)imm));
	athrill_jit_alu(b, ATHRILL_JIT_AND, TJ_T0, TJ_T0, athrill_jit_imm(0xFFFFFFFFLL));
}

/* jump to slow unless [T0, T0 + size) lies in region (T1 scratch) */
static void thumb_jit_guard(AthrillJitBuilder *b, const ThumbJitRegion *r, uint32 size, AthrillJitLabel *outside)
{
	athrill_jit_alu(b, ATHRILL_JIT_SUB, TJ_T1, TJ_T0, athrill_jit_imm((sint64)r->start));
	athrill_jit_jump_if(b, ATHRILL_JIT_GTU, TJ_T1, athrill_jit_imm((sint64)(r->size - size)), outside);
}

static sint64 thumb_jit_bias(const ThumbJitRegion *r, uint32 offset)
{
	return (sint64)((uint64)(uintptr_t)r->data - r->start + offset);
}


/* R[t] = MemU[T0, size] (zero-extended) from RAM or ROM; else slow */
static void thumb_jit_load(AthrillJitBuilder *b, uint32 t, uint32 size, AthrillJitLabel *slow)
{
	AthrillJitLabel *not_ram = athrill_jit_label_new(b);
	AthrillJitLabel *done = athrill_jit_label_new(b);

	thumb_jit_guard(b, &thumb_jit_ram, size, not_ram);
	athrill_jit_load_host(b, TJ_T1, TJ_T0, athrill_jit_imm(thumb_jit_bias(&thumb_jit_ram, 0U)), (uint8)size, FALSE);
	athrill_jit_mov(b, TJ_R(t), TJ_T1);
	athrill_jit_jump(b, done);
	athrill_jit_label_place(b, not_ram);
	if (!thumb_jit_rom.known) {
		athrill_jit_jump(b, slow);
	} else {
		thumb_jit_guard(b, &thumb_jit_rom, size, slow);
		athrill_jit_load_host(b, TJ_T1, TJ_T0, athrill_jit_imm(thumb_jit_bias(&thumb_jit_rom, 0U)), (uint8)size, FALSE);
		athrill_jit_mov(b, TJ_R(t), TJ_T1);
	}
	athrill_jit_label_place(b, done);
}

/* MemU[T0, size] = R[t] in RAM; else slow */
static void thumb_jit_store(AthrillJitBuilder *b, uint32 t, uint32 size, AthrillJitLabel *slow)
{
	thumb_jit_guard(b, &thumb_jit_ram, size, slow);
	athrill_jit_store_host(b, TJ_R(t), TJ_T0, athrill_jit_imm(thumb_jit_bias(&thumb_jit_ram, 0U)), (uint8)size);
}

/*
 * Emits a load or store natively with its C path in *slow (filled in);
 * FALSE: not one of these. POP {.., PC} goes on to the shared taken exit.
 */
static std_bool thumb_jit_native_memory(AthrillJitBuilder *b, const ThumbDecodedType *e, ThumbJitSlow *slow,
					AthrillJitLabel *taken)
{
	uint16 insn = (uint16)e->insn;
	uint32 registers;
	uint32 count;
	uint32 i;
	uint32 k;

	if ((e->length != 2U) || !thumb_jit_ram.known) {
		return FALSE;
	}
	switch (t32_decode16(insn)) {
	case T32_16_LDR_i_T1:
	case T32_16_LDR_i_T2:
	case T32_16_LDRB_i_T1:
	case T32_16_STR_i_T1:
	case T32_16_STR_i_T2:
	case T32_16_STRB_i_T1:
	case T32_16_PUSH_T1:
	case T32_16_POP_T1:
		break;
	default:
		return FALSE;
	}
	if (((t32_decode16(insn) == T32_16_PUSH_T1) && ((((t32_PUSH_T1_M(insn) << 14) | (insn & 0xFFU))) == 0U))
		|| ((t32_decode16(insn) == T32_16_POP_T1) && ((((t32_POP_T1_P(insn) << 15) | (insn & 0xFFU))) == 0U))) {
		return FALSE;	/* no registers: UNPREDICTABLE, the executor decides */
	}
	slow->start = athrill_jit_label_new(b);
	slow->resume = athrill_jit_label_new(b);
	slow->entry = e;
	switch (t32_decode16(insn)) {
	case T32_16_LDR_i_T1:
		thumb_jit_address(b, t32_LDR_i_T1_Rn(insn), t32_LDR_i_T1_imm5(insn) << 2);
		thumb_jit_load(b, t32_LDR_i_T1_Rt(insn), 4U, slow->start);
		break;
	case T32_16_LDR_i_T2:
		thumb_jit_address(b, 13U, t32_LDR_i_T2_imm8(insn) << 2);
		thumb_jit_load(b, t32_LDR_i_T2_Rt(insn), 4U, slow->start);
		break;
	case T32_16_LDRB_i_T1:
		thumb_jit_address(b, t32_LDRB_i_T1_Rn(insn), t32_LDRB_i_T1_imm5(insn));
		thumb_jit_load(b, t32_LDRB_i_T1_Rt(insn), 1U, slow->start);
		break;
	case T32_16_STR_i_T1:
		thumb_jit_address(b, t32_STR_i_T1_Rn(insn), t32_STR_i_T1_imm5(insn) << 2);
		thumb_jit_store(b, t32_STR_i_T1_Rt(insn), 4U, slow->start);
		break;
	case T32_16_STR_i_T2:
		thumb_jit_address(b, 13U, t32_STR_i_T2_imm8(insn) << 2);
		thumb_jit_store(b, t32_STR_i_T2_Rt(insn), 4U, slow->start);
		break;
	case T32_16_STRB_i_T1:
		thumb_jit_address(b, t32_STRB_i_T1_Rn(insn), t32_STRB_i_T1_imm5(insn));
		thumb_jit_store(b, t32_STRB_i_T1_Rt(insn), 1U, slow->start);
		break;
	case T32_16_PUSH_T1:
		/* fast_push_t1(): the registers to SP - 4n (word aligned), SP = SP - 4n */
		registers = (t32_PUSH_T1_M(insn) << 14) | (insn & 0xFFU);
		count = (uint32)__builtin_popcount(registers);
		if (count == 0U) {
			return FALSE;
		}
		thumb_jit_address(b, 13U, (uint32)(0U - 4U * count));
		athrill_jit_alu(b, ATHRILL_JIT_AND, TJ_T1, TJ_T0, athrill_jit_imm(3));
		athrill_jit_jump_if(b, ATHRILL_JIT_NE, TJ_T1, athrill_jit_imm(0), slow->start);
		thumb_jit_guard(b, &thumb_jit_ram, 4U * count, slow->start);
		for (i = 0U, k = 0U; i < 15U; i++) {
			if ((registers & (1U << i)) != 0U) {
				athrill_jit_store_host(b, TJ_R(i), TJ_T0, athrill_jit_imm(thumb_jit_bias(&thumb_jit_ram, 4U * k)), 4U);
				k++;
			}
		}
		athrill_jit_mov(b, TJ_R(13U), TJ_T0);
		break;
	case T32_16_POP_T1:
		/* fast_pop_t1(): the registers from SP (word aligned), SP = SP + 4n, PC by LoadWritePC */
		registers = (t32_POP_T1_P(insn) << 15) | (insn & 0xFFU);
		count = (uint32)__builtin_popcount(registers);
		if (count == 0U) {
			return FALSE;
		}
		thumb_jit_address(b, 13U, 0U);
		athrill_jit_alu(b, ATHRILL_JIT_AND, TJ_T1, TJ_T0, athrill_jit_imm(3));
		athrill_jit_jump_if(b, ATHRILL_JIT_NE, TJ_T1, athrill_jit_imm(0), slow->start);
		thumb_jit_guard(b, &thumb_jit_ram, 4U * count, slow->start);
		if ((registers & (1U << 15)) != 0U) {
			/* the PC value first: an EXC_RETURN value or a clear bit 0 (EPSR.T) takes the C path */
			athrill_jit_load_host(b, TJ_T1, TJ_T0, athrill_jit_imm(thumb_jit_bias(&thumb_jit_ram, 4U * (count - 1U))), 4U, FALSE);
			athrill_jit_mov(b, TJ_CTX(fx, 4), TJ_T1);
			athrill_jit_alu(b, ATHRILL_JIT_AND, TJ_T1, TJ_T1, athrill_jit_imm(0xF0000000LL));
			athrill_jit_jump_if(b, ATHRILL_JIT_EQ, TJ_T1, athrill_jit_imm(0xF0000000LL), slow->start);
			athrill_jit_alu(b, ATHRILL_JIT_AND, TJ_T1, TJ_CTX(fx, 4), athrill_jit_imm(1));
			athrill_jit_jump_if(b, ATHRILL_JIT_EQ, TJ_T1, athrill_jit_imm(0), slow->start);
		}
		for (i = 0U, k = 0U; i < 15U; i++) {
			if ((registers & (1U << i)) != 0U) {
				athrill_jit_load_host(b, TJ_T1, TJ_T0, athrill_jit_imm(thumb_jit_bias(&thumb_jit_ram, 4U * k)), 4U, FALSE);
				athrill_jit_mov(b, TJ_R(i), TJ_T1);
				k++;
			}
		}
		athrill_jit_alu(b, ATHRILL_JIT_ADD, TJ_T0, TJ_T0, athrill_jit_imm((sint64)(4U * count)));
		athrill_jit_mov(b, TJ_R(13U), TJ_T0);
		if ((registers & (1U << 15)) != 0U) {
			/* BXWritePC: EPSR.T stays 1, next_pc = the value without bit 0; on as a taken branch */
			athrill_jit_mov(b, TJ_CORE(reg.pc, 4), athrill_jit_imm((sint64)e->pc));
			athrill_jit_alu(b, ATHRILL_JIT_AND, TJ_CORE(reg.next_pc, 4), TJ_CTX(fx, 4), athrill_jit_imm(0xFFFFFFFELL));
#ifdef ATHRILL_STM32_JIT_DIFF_TEST
			thumb_jit_diff_count(b, &thumb_jit_diff_stats.memory_native);
#endif
			athrill_jit_jump(b, taken);
		}
		break;
	default:
		return FALSE;
	}
#ifdef ATHRILL_STM32_JIT_DIFF_TEST
	thumb_jit_diff_count(b, &thumb_jit_diff_stats.memory_native);
#endif
	athrill_jit_label_place(b, slow->resume);
	return TRUE;
}

static AthrillJitCode *thumb_jit_translate(uint32 start, uint32 *length_out)
{
	AthrillJitBuilder *b;
	AthrillAddrType idle_pc = athrill_idle_spin_halt_idle_pc();
	ThumbDecodedType *list[THUMB_JIT_MAX_LENGTH];
	uint32 length = 0U;
	uint32 pc = start;
	uint32 i;
	std_bool ends_with_it = FALSE;
	AthrillJitLabel *complete_exit;
	AthrillJitLabel *taken;
	AthrillJitLabel *self_exit;
	ThumbJitTaken taken_exits[THUMB_JIT_MAX_LENGTH];
	uint32 taken_count = 0U;
	ThumbJitSlow slows[THUMB_JIT_MAX_LENGTH];
	uint32 slow_count = 0U;
	std_bool native_end = FALSE;

	while (length < THUMB_JIT_MAX_LENGTH) {
		ThumbDecodedType *e = thumb_jit_entry(pc);

		if ((e == NULL) || thumb_jit_is_host(e) || ((length != 0U) && ((AthrillAddrType)pc == idle_pc))) {
			break;
		}
		list[length] = e;
		length++;
		pc += e->length;
		if (thumb_jit_is_it(e)) {
			ends_with_it = TRUE;
			break;
		}
		if (thumb_jit_is_unconditional_branch(e)) {
			break;
		}
	}
	if (length == 0U) {
		return NULL;
	}
	b = athrill_jit_builder_create();
	if (b == NULL) {
		return NULL;
	}
	complete_exit = athrill_jit_label_new(b);
	taken = athrill_jit_label_new(b);
	self_exit = athrill_jit_label_new(b);
#ifdef ATHRILL_STM32_JIT_DIFF_TEST
	thumb_jit_diff_count(b, &thumb_jit_diff_stats.block_entries);
#endif
	/* entry check: a chained run stops before a block that would go over its budget */
	athrill_jit_alu(b, ATHRILL_JIT_ADD, TJ_T0, athrill_jit_done(), athrill_jit_imm((sint64)length));
	athrill_jit_exit_if(b, ATHRILL_JIT_GTU, TJ_T0, TJ_CTX(budget, 8));
	/* set by an instruction only when the block ends right after it (complete()) */
	athrill_jit_mov(b, TJ_CORE(it_written, 4), athrill_jit_imm(0));
	athrill_jit_mov(b, TJ_CORE(sync_exception, 4), athrill_jit_imm(0));
	athrill_jit_mov(b, TJ_CORE(exc_return_pending, 4), athrill_jit_imm(0));
	for (i = 0U; i < length; i++) {
		const ThumbDecodedType *e = list[i];
		uint32 next = e->pc + e->length;
		uint32 kind = thumb_jit_kind(e);
		std_bool ends = FALSE;

		athrill_jit_count(b, 1U);
		if (thumb_jit_native(b, e, &ends, taken_exits, &taken_count)) {
			if (ends) {
				native_end = TRUE;
				break;
			}
			continue;
		}
		if (thumb_jit_native_memory(b, e, &slows[slow_count], taken)) {
			slow_count++;
			continue;
		}
		(void)next;
		thumb_jit_emit_call(b, e, kind, complete_exit, taken);
	}
	if (!native_end) {
		/* PC after the last instruction (a native one does not write it) */
		uint32 end_pc = list[length - 1U]->pc + list[length - 1U]->length;

		athrill_jit_mov(b, TJ_CORE(reg.pc, 4), athrill_jit_imm((sint64)end_pc));
		athrill_jit_mov(b, TJ_CORE(reg.next_pc, 4), athrill_jit_imm((sint64)end_pc));
		if (!ends_with_it) {
			thumb_jit_exit_to(b, end_pc);
		}
	}
	athrill_jit_exit(b);
	/* the C paths of the native loads and stores: the instruction called, then back */
	for (i = 0U; i < slow_count; i++) {
		athrill_jit_label_place(b, slows[i].start);
		thumb_jit_emit_call(b, slows[i].entry, THUMB_JIT_MEMORY, complete_exit, taken);
		athrill_jit_jump(b, slows[i].resume);
	}
	/* the taken exits of the native conditional branches */
	for (i = 0U; i < taken_count; i++) {
		athrill_jit_label_place(b, taken_exits[i].label);
		athrill_jit_mov(b, TJ_CORE(reg.pc, 4), athrill_jit_imm((sint64)taken_exits[i].target));
		athrill_jit_mov(b, TJ_CORE(reg.next_pc, 4), athrill_jit_imm((sint64)taken_exits[i].target));
		thumb_jit_exit_to(b, taken_exits[i].target);
		athrill_jit_exit(b);
	}
	/* complete(): an error, an exception return, a synchronous exception (PC still the instruction's) */
	athrill_jit_label_place(b, complete_exit);
	athrill_jit_mov(b, TJ_CTX(err, 4), athrill_jit_result());
	athrill_jit_mov(b, TJ_CTX(exit_kind, 4), athrill_jit_imm(THUMB_JIT_EXIT_COMPLETE));
	athrill_jit_exit(b);
	/* a branch: to itself (C), or on through the table unless C has something to do */
	athrill_jit_label_place(b, taken);
	athrill_jit_jump_if(b, ATHRILL_JIT_EQ, TJ_CORE(reg.next_pc, 4), TJ_CORE(reg.pc, 4), self_exit);
	athrill_jit_mov(b, TJ_CORE(reg.pc, 4), TJ_CORE(reg.next_pc, 4));
	thumb_jit_checks(b, THUMB_JIT_OTHER);
	athrill_jit_exit_if(b, ATHRILL_JIT_NE, TJ_CORE(reg.itstate, 4), athrill_jit_imm(0));
	athrill_jit_mov(b, TJ_T0, TJ_CORE(reg.next_pc, 4));
	athrill_jit_chain_indirect(b, TJ_T0);
	athrill_jit_mov(b, TJ_CTX(indirect_miss, 8), athrill_jit_imm(1));
	athrill_jit_exit(b);
	athrill_jit_label_place(b, self_exit);
	athrill_jit_mov(b, TJ_CTX(exit_kind, 4), athrill_jit_imm(THUMB_JIT_EXIT_SELF));
	athrill_jit_exit(b);
	*length_out = length;
	return athrill_jit_builder_finish(b);
}

/*
 * Runs the block at PC as thumb_execute_run() runs its instructions; returns
 * the instructions executed (0: none, the caller runs execute_one()), *err
 * the result of the last one.
 */
static inline uint64 thumb_jit_run(TargetCoreType *core, uint64 budget, uint64 *clock, Std_ReturnType *err)
{
	uint32 pc = core->reg.pc;
	ThumbDecodedType *e;
	AthrillJitCode *fresh;
	const AthrillJitCode *code;
	uint32 length = 0U;
	uint64 done;

	*err = STD_E_OK;
	if (thumb_jit_clock == NULL) {
		thumb_jit_clock = clock;
		/* plain memory for the native loads and stores: the stack's RAM, the code's ROM */
		thumb_jit_find_region(&thumb_jit_ram, core->reg.r[13], TRUE);
		thumb_jit_find_region(&thumb_jit_rom, pc, FALSE);
	}
	if ((clock != thumb_jit_clock) || core->m.pending_check || core->m.priority_check || core->m.lockup
		|| !core->m.epsr_t || (core->reg.itstate != 0U)) {
#ifdef ATHRILL_STM32_JIT_DIFF_TEST
		if (core->reg.itstate != 0U) {
			thumb_jit_diff_stats.it_fallbacks++;
		}
#endif
		return 0U;
	}
	e = thumb_jit_entry(pc);
	if ((e == NULL) || thumb_jit_is_host(e)) {
		return 0U;
	}
	if ((e->jit_code == NULL) || (e->jit_epoch != thumb_jit_epoch)) {
		if ((e->jit_failed_epoch == thumb_jit_epoch) && (e->jit_code == NULL)) {
			return 0U;	/* this block could not be translated in this epoch */
		}
		fresh = thumb_jit_translate(pc, &length);
		if (fresh == NULL) {
			e->jit_failed_epoch = thumb_jit_epoch;
			thumb_jit_failures++;
			return 0U;
		}
		athrill_jit_cache_insert(thumb_jit_cache, pc, 0U, thumb_jit_epoch, fresh);
		thumb_jit_pending_slot = NULL;	/* the insert may have freed the block it belongs to */
		thumb_jit_pending_indirect = FALSE;
		thumb_jit_translated++;
		e->jit_code = fresh;
		e->jit_epoch = thumb_jit_epoch;
		e->jit_length = length;
	}
	if (e->jit_length > budget) {
#ifdef ATHRILL_STM32_JIT_DIFF_TEST
		thumb_jit_diff_stats.budget_fallbacks++;
#endif
		return 0U;
	}
	code = (const AthrillJitCode *)e->jit_code;
	/* the run before ended at an exit to this PC: link it here */
	if ((thumb_jit_pending_slot != NULL) && (athrill_jit_chain_tag(thumb_jit_pending_slot) == pc)) {
		athrill_jit_chain_link(thumb_jit_pending_slot, code);
	}
	thumb_jit_pending_slot = NULL;
	if (thumb_jit_pending_indirect && ((AthrillAddrType)pc != athrill_idle_spin_halt_idle_pc())) {
		athrill_jit_chain_indirect_set(pc, code);
	}
	thumb_jit_pending_indirect = FALSE;
	thumb_jit_ctx.clock_base = *clock;
	thumb_jit_ctx.budget = budget;
	thumb_jit_ctx.chain_slot = 0U;
	thumb_jit_ctx.indirect_miss = 0U;
	thumb_jit_ctx.exit_kind = THUMB_JIT_EXIT_NONE;
	thumb_jit_ctx.err = 0U;
	done = athrill_jit_code_run(code, core, &thumb_jit_ctx);
	*clock = thumb_jit_ctx.clock_base + done;
	thumb_jit_runs++;
	thumb_jit_insns += done;
	thumb_jit_pending_slot = (AthrillJitChainSlot *)(uintptr_t)thumb_jit_ctx.chain_slot;
	thumb_jit_pending_indirect = (thumb_jit_ctx.indirect_miss != 0U);
	if (thumb_jit_ctx.exit_kind == THUMB_JIT_EXIT_COMPLETE) {
		/* the last instruction asked for complete(): PC is still its own */
		*err = complete(core, (Std_ReturnType)thumb_jit_ctx.err);
	} else if (thumb_jit_ctx.exit_kind == THUMB_JIT_EXIT_SELF) {
		const ThumbDecodedType *self = thumb_jit_entry(core->reg.pc);

		if ((self != NULL) && self->branch_to_self) {
			athrill_idle_spin_halt_after_execute(core->core_id, core->reg.pc);
		}
	}
	return done;
}

#endif /* ATHRILL_ENABLE_JIT */

Std_ReturnType thumb_execute_run(TargetCoreType *core, uint64 max_clocks, uint64 *clock, uint64 *executed)
{
	Std_ReturnType err = STD_E_OK;
	uint64 n = 0U;
	/* idle_ready() is FALSE at any other PC: asked only there */
	AthrillAddrType idle_pc = athrill_idle_spin_halt_idle_pc();

	while ((n < max_clocks) && !core->is_halt && !stm32_device_accessed && !core->reset_pending
		&& !core->scs.reset_requested) {
		if (((AthrillAddrType)core->reg.pc == idle_pc) && athrill_idle_spin_halt_idle_ready(core->core_id)) {
			break;
		}
#ifdef ATHRILL_ENABLE_JIT
		if (thumb_jit_enabled()) {
			uint64 done = thumb_jit_run(core, max_clocks - n, clock, &err);

			if (done != 0U) {
				n += done;
				if (err != STD_E_OK) {
					break;
				}
				continue;
			}
		}
#endif
		(*clock)++;
		n++;
		err = execute_one(core);
		if (err != STD_E_OK) {
			break;
		}
	}
	*executed = n;
	return err;
}

const char *thumb_instruction_at(const TargetCoreType *core, uint32 address, uint32 *insn, uint32 *length)
{
	*insn = 0U;
	*length = 2U;
	if (fetch(core, address, insn, length) != STD_E_OK) {
		return "(not readable)";
	}
	if (*length == 2U) {
		return t32_decode16_names[t32_decode16((uint16)*insn)];
	}
	return t32_decode32_names[t32_decode32(*insn)];
}

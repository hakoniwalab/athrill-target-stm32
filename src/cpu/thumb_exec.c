/*
 * Fetch and dispatch of Thumb instructions: a halfword whose bits 15:11 are
 * 0b11101, 0b11110 or 0b11111 starts a 32-bit instruction.
 */
#include "thumb_exec.h"
#include "t32_decode16.h"
#include "t32_decode32.h"
#include "arm_common/arm_m_exception.h"
#include "memory_window.h"
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
#define DECODE_CACHE_BITS	16U
#define DECODE_CACHE_SIZE	(1U << DECODE_CACHE_BITS)

typedef struct {
	uint32 pc;		/* odd: empty */
	uint32 insn;		/* as fetch() returns it */
	uint32 length;
	ThumbExec16 exec16;
	ThumbExec32 exec32;
} ThumbDecodedType;

static ThumbDecodedType decode_cache[DECODE_CACHE_SIZE];
static std_bool decode_cache_ready = FALSE;

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

/* A miss: the instruction at pc decoded into its cache entry; NULL when pc is not in memory. */
COLD_PATH static const ThumbDecodedType *decode_into(ThumbDecodedType *e, uint32 pc)
{
	const uint8 *p = memory_window_pointer(pc, 2U);
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
	if (t32_decode16(hw1) != T32_16_WIDE) {
		e->pc = pc;
		e->insn = hw1;
		e->length = 2U;
		e->exec16 = thumb_executor16(hw1);
		if (e->exec16 == NULL) {
			e->exec16 = undefined16;
		}
		e->exec32 = NULL;
		return e;
	}
	p2 = memory_window_pointer(pc + 2U, 2U);
	if (p2 == NULL) {
		return NULL;
	}
	e->pc = pc;
	e->insn = ((uint32)hw1 << 16) | halfword(p2);
	e->length = 4U;
	e->exec16 = NULL;
	e->exec32 = thumb_executor32(e->insn);
	if (e->exec32 == NULL) {
		e->exec32 = undefined32;
	}
	return e;
}

/* The cached instruction at pc, decoded on a miss; NULL when pc is not in memory. */
static inline const ThumbDecodedType *decoded_at(uint32 pc)
{
	ThumbDecodedType *e = &decode_cache[(pc >> 1) & (DECODE_CACHE_SIZE - 1U)];
	const uint8 *p;

	if (e->pc == pc) {
		p = memory_window_pointer(pc, e->length);
		if ((p != NULL) && ((e->length == 2U) ? (e->insn == halfword(p))
			: (e->insn == (((uint32)halfword(p) << 16) | halfword(p + 2U))))) {
			return e;
		}
	}
	return decode_into(e, pc);
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
Std_ReturnType thumb_execute(TargetCoreType *core)
{
	const ThumbDecodedType *decoded;
	Std_ReturnType err;

	if (core->m.pending_check || core->m.lockup || !core->m.epsr_t) {
		return execute_slow(core);
	}
	decoded = decoded_at(core->reg.pc);
	if (decoded == NULL) {
		return execute_slow(core);
	}
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
	return STD_E_OK;
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

/*
 * Thumb instruction execution, split by instruction length.
 *
 * Each executor returns STD_E_OK, STD_E_DECODE for an instruction it does
 * not execute yet, STD_E_INVALID for an UNPREDICTABLE case, or the error of a
 * fault (which stops the run; exceptions are not modelled yet).
 */
#ifndef ATHRILL_STM32_THUMB_EXEC_H
#define ATHRILL_STM32_THUMB_EXEC_H

#include "arm_target_profile.h"

typedef Std_ReturnType (*ThumbExec16)(TargetCoreType *core, uint16 insn);
typedef Std_ReturnType (*ThumbExec32)(TargetCoreType *core, uint32 insn);

/* The executors generated from tools/glue_table.py (thumb_gen16.c, thumb_gen32.c),
 * by generated decoder id; the hand-written ones are in thumb_exec16.c and
 * thumb_exec32.c. An id has at most one of the two. */
extern const ThumbExec16 thumb_gen_executors16[];
extern const ThumbExec32 thumb_gen_executors32[];

/* The executor of an instruction, or NULL (UNDEFINED, or not executed yet). */
ThumbExec16 thumb_executor16(uint16 insn);
ThumbExec32 thumb_executor32(uint32 insn);

Std_ReturnType thumb_execute16(TargetCoreType *core, uint16 insn);
/* insn: the first halfword in bits 31:16, the second in bits 15:0. */
Std_ReturnType thumb_execute32(TargetCoreType *core, uint32 insn);

/* Hook for the core-owned branch-to-self halt policy. */
std_bool thumb_is_branch_to_self(CoreIdType core_id, AthrillAddrType pc);

#endif /* ATHRILL_STM32_THUMB_EXEC_H */

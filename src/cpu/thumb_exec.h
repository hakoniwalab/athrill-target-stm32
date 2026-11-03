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

Std_ReturnType thumb_execute16(TargetCoreType *core, uint16 insn);
/* insn: the first halfword in bits 31:16, the second in bits 15:0. */
Std_ReturnType thumb_execute32(TargetCoreType *core, uint32 insn);

#endif /* ATHRILL_STM32_THUMB_EXEC_H */

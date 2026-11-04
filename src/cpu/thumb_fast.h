/*
 * Fast executors of the most frequent Thumb encodings in PX4 (measured on
 * PX4 SIH): the same behaviour as the generic executors that go through
 * athrill-arm-common's pseudo-code, without its input and output structures.
 * Used when DEVICE_CONFIG_ARM_EXEC_MODE is fast (the default); strict uses
 * the generic executors only.
 */
#ifndef ATHRILL_STM32_THUMB_FAST_H
#define ATHRILL_STM32_THUMB_FAST_H

#include "thumb_exec.h"

/* The fast executor of an instruction, or NULL (the generic one is used). */
ThumbExec16 thumb_fast_executor16(uint16 insn);
ThumbExec32 thumb_fast_executor32(uint32 insn);

/* DEVICE_CONFIG_ARM_EXEC_MODE: TRUE for fast (default), FALSE for strict. */
extern std_bool stm32_exec_fast;

#endif /* ATHRILL_STM32_THUMB_FAST_H */

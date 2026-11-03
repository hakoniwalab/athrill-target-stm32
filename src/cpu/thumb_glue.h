/*
 * Glue from Thumb encodings to athrill-arm-common's data-processing
 * pseudo-code: each encoding's executor decodes its fields and checks its
 * UNPREDICTABLE cases ("Encoding specific operations"), then calls one of
 * these with the operands.
 */
#ifndef ATHRILL_STM32_THUMB_GLUE_H
#define ATHRILL_STM32_THUMB_GLUE_H

#include "thumb_exec.h"
#include "arm_common/arm_pseudo_arith.h"
#include "arm_common/arm_pseudo_logic.h"
#include "arm_common/arm_pseudo_move.h"

typedef Std_ReturnType (*ThumbDpImm)(TargetCoreType *core, const arm_dp_imm_input_type *in, arm_dp_output_type *out);
typedef Std_ReturnType (*ThumbDpReg)(TargetCoreType *core, const arm_dp_reg_input_type *in, arm_dp_output_type *out);

/* Register numbers that mark an operand an instruction does not have. */
#define THUMB_NO_REG	0xFFFFFFFFU

static inline void thumb_set_reg(TargetCoreType *core, PseudoCodeRegisterDataType *reg, const char *name, uint32 n)
{
	if (n == THUMB_NO_REG) {
		reg->name = NULL;
		reg->regId = 0U;
		reg->regData = 0U;
		return;
	}
	reg->name = name;
	reg->regId = n;
	reg->regData = ArmReadReg(core, n);
}

/* An immediate form: op(d, n, imm32) with the expansion's carry. */
static inline Std_ReturnType thumb_dp_imm(TargetCoreType *core, ThumbDpImm op, const char *name, bool setflags,
	uint32 d, uint32 n, uint32 imm32, bool carry)
{
	arm_dp_imm_input_type in;
	arm_dp_output_type out;

	in.instrName = name;
	in.cond = Stm32CurrentCond(core);
	in.setflags = setflags;
	thumb_set_reg(core, &in.Rd, "Rd", d);
	thumb_set_reg(core, &in.Rn, "Rn", n);
	in.imm32 = imm32;
	in.carry = carry;
	return op(core, &in, &out);
}

/* A register form: op(d, n, Shift(R[m], shift_t, shift_n)). */
static inline Std_ReturnType thumb_dp_reg(TargetCoreType *core, ThumbDpReg op, const char *name, bool setflags,
	uint32 d, uint32 n, uint32 m, SRType shift_t, uint32 shift_n)
{
	arm_dp_reg_input_type in;
	arm_dp_output_type out;

	in.instrName = name;
	in.cond = Stm32CurrentCond(core);
	in.setflags = setflags;
	thumb_set_reg(core, &in.Rd, "Rd", d);
	thumb_set_reg(core, &in.Rn, "Rn", n);
	thumb_set_reg(core, &in.Rm, "Rm", m);
	in.shift_t = shift_t;
	in.shift_n = shift_n;
	return op(core, &in, &out);
}

/* MOV (register-shifted register): d = Shift(R[m], shift_t, R[s]<7:0>). */
static inline Std_ReturnType thumb_mov_rsr(TargetCoreType *core, const char *name, bool setflags,
	uint32 d, uint32 m, uint32 s, SRType shift_t)
{
	arm_dp_rsr_input_type in;
	arm_dp_output_type out;

	in.instrName = name;
	in.cond = Stm32CurrentCond(core);
	in.setflags = setflags;
	thumb_set_reg(core, &in.Rd, "Rd", d);
	thumb_set_reg(core, &in.Rn, "Rn", THUMB_NO_REG);
	thumb_set_reg(core, &in.Rm, "Rm", m);
	thumb_set_reg(core, &in.Rs, "Rs", s);
	in.shift_t = shift_t;
	return arm_pseudo_mov_rsr(core, &in, &out);
}

/* SP or PC: the registers most 32-bit encodings make UNPREDICTABLE. */
static inline bool thumb_sp_or_pc(uint32 n)
{
	return (n == 13U) || (n == 15U);
}

/* i:imm3:imm8, the 12-bit modified immediate of 32-bit data processing. */
static inline uint32 thumb_imm12(uint32 i, uint32 imm3, uint32 imm8)
{
	return (i << 11) | (imm3 << 8) | imm8;
}

/* imm3:imm2, the 5-bit shift amount of 32-bit register forms. */
static inline uint32 thumb_imm5(uint32 imm3, uint32 imm2)
{
	return (imm3 << 2) | imm2;
}

#endif /* ATHRILL_STM32_THUMB_GLUE_H */

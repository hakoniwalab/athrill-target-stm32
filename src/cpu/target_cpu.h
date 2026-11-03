#ifndef ATHRILL_STM32_TARGET_CPU_H
#define ATHRILL_STM32_TARGET_CPU_H

#include "std_types.h"
#include "arm_common/arm_m_types.h"

#define ATHRILL_TARGET_ARCH "ARMv7-M"
#define ATHRILL_TARGET_VERSION "0.1.0"
#define CPU_REG_SP 13U
#define CPU_REG_LR 14U
#define CPU_REG_PC 15U

typedef enum { CpuMemoryAccess_NONE = 0, CpuMemoryAccess_READ, CpuMemoryAccess_WRITE, CpuMemoryAccess_EXEC } CpuMemoryAccessType;
typedef enum { OpCodeId_Illegal = 0, OpCodeId_Num } OpCodeId;
typedef struct { uint32 instruction; uint8 length; } OpDecodedCodeType;

/* Vector table address at reset (VTOR): STM32 booting from its internal flash. */
#define STM32_RESET_VTOR 0x08000000U

typedef struct {
    uint32 r[16];	/* r13 is the current stack pointer; r15 is not used (see pc) */
    uint32 pc;	/* address of the instruction being executed */
    uint32 next_pc;	/* address of the next instruction (written by a branch) */
    uint32 apsr;
    uint32 itstate;	/* EPSR.IT<7:0> (ITSTATE) */
    uint32 s[32];	/* S0-S31; D[n] = S[2n+1]:S[2n] (FPv5-D16: D0-D15) */
    uint32 fpscr;
} CpuRegisterType;

typedef struct TargetCoreTypeTag {
    CoreIdType core_id;
    CpuRegisterType reg;
    std_bool is_halt;
    std_bool reset_pending;	/* SP and PC still to be read from the vector table */
    std_bool access_fault;
    std_bool exclusive_valid;	/* local exclusive monitor (LDREX/STREX) */
    uint32 exclusive_address;
    std_bool it_written;	/* this instruction was IT: the IT state does not advance */
    ArmMStateType m;		/* the exception model (athrill-arm-common arm_m_exception.h) */
    ArmMSysTickType systick;
    ArmMScsConfigType scs;
    uint32 sync_exception;	/* an exception this instruction raised (SVCall, a fault), taken after it */
    uint32 sync_return;		/* its return address */
    std_bool exc_return_pending;	/* this instruction wrote an EXC_RETURN value to PC in Handler mode */
    uint32 exc_return;
    std_bool wfe;		/* halted by WFE (an event also wakes it) */
    OpDecodedCodeType *decoded_code;
    uint64 internal_cycle;
} TargetCoreType;

static inline AthrillAddrType cpu_get_pc(const TargetCoreType *core) { return core->reg.pc; }
static inline uint32 cpu_get_sp(const TargetCoreType *core) { return core->reg.r[CPU_REG_SP]; }

/* Executes one Thumb instruction at core->reg.pc (thumb_exec.c). */
Std_ReturnType thumb_execute(TargetCoreType *core);
/*
 * The instruction at address as the generated decoders name it (for
 * messages); *insn and *length get its encoding (first halfword in bits 31:16
 * of a 32-bit one) and its length in bytes.
 */
const char *thumb_instruction_at(const TargetCoreType *core, uint32 address, uint32 *insn, uint32 *length);

#endif

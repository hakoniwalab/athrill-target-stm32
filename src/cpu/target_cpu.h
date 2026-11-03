#ifndef ATHRILL_STM32_TARGET_CPU_H
#define ATHRILL_STM32_TARGET_CPU_H

#include "std_types.h"

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

/* APSR condition flags (bits 31-28). */
#define APSR_N (1U << 31)
#define APSR_Z (1U << 30)
#define APSR_C (1U << 29)
#define APSR_V (1U << 28)

typedef struct {
    uint32 r[16];	/* r13 is the current stack pointer; r15 is not used (see pc) */
    uint32 pc;	/* address of the instruction being executed */
    uint32 apsr;
} CpuRegisterType;

typedef struct TargetCoreTypeTag {
    CoreIdType core_id;
    CpuRegisterType reg;
    std_bool is_halt;
    std_bool reset_pending;	/* SP and PC still to be read from the vector table */
    std_bool access_fault;
    OpDecodedCodeType *decoded_code;
    uint64 internal_cycle;
} TargetCoreType;

static inline AthrillAddrType cpu_get_pc(const TargetCoreType *core) { return core->reg.pc; }
static inline uint32 cpu_get_sp(const TargetCoreType *core) { return core->reg.r[CPU_REG_SP]; }

/* Executes one Thumb instruction at core->reg.pc (thumb_bootstrap.c). */
Std_ReturnType thumb_execute(TargetCoreType *core);

#endif

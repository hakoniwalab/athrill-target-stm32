# Instructions of the NuttX start-up

Which Thumb instructions PX4's NuttX firmware for FMUv5 (STM32F765) runs while
it starts, counted by `tools/insn_usage.py`, to decide what to execute next.

## How it is counted

The firmware is PX4 `px4_fmu-v5_default`, built from PX4 commit
`216039a12b` in the toolchain image (`px4io/px4-dev:v1.17.0-beta1`). Its
disassembly (GNU objdump) is decoded with this target's generated decoders,
so every instruction is named by its encoding in Arm's data (`LDR_i_T3`, ...).
The start-up is the code reachable from the reset entry `__start` through
direct branches and calls; calls through pointers are not followed, so it is
a lower bound of what runs.

With PX4 built in `../PX4-Autopilot` and this target built in `build` (with
`BUILD_TESTING`, for `decode_dump`), from this checkout:

```bash
docker run --rm -v "$PWD/../PX4-Autopilot/build/px4_fmu-v5_default:/w" -w /w athrill-stm32-toolchain sh -c 'arm-none-eabi-objdump -d px4_fmu-v5_default.elf > fmuv5.lst'
python tools/insn_usage.py ../PX4-Autopilot/build/px4_fmu-v5_default/fmuv5.lst build/decode_dump __start --stop=nx_start
python tools/insn_usage.py ../PX4-Autopilot/build/px4_fmu-v5_default/fmuv5.lst build/decode_dump __start
```

## Result

| Part | Functions | Instructions | Encodings used | Executed now |
|---|---|---|---|---|
| Before the OS (`__start` without `nx_start`) | 30 | 1,087 | 93 | 21 |
| With the OS start (`nx_start` and what it calls directly) | 358 | 11,637 | 184 | 26 |
| Whole firmware | 11,628 | 483,143 | | |

Before the OS, the code is the chip and board set-up: `__start`,
`stm32_clockconfig`, `stm32_lowsetup`, `stm32_boardinitialize`,
`mpu_early_reset`, `up_enable_icache`/`dcache`, `arm_fpuconfig`,
`arm_earlyserialinit`, GPIO configuration, `memcpy`/`memset`.

### Before the OS: 57 instructions

By instruction (all encodings of one instruction share one pseudo-code
function). Done: B, LDR (immediate), LDR (literal), ADD (immediate), NOP, BX.

| Kind | Instructions (uses) |
|---|---|
| Data processing, immediate | MOV (84), CMP (40), ORR (38), BIC (34), SUB (26), AND (20), RSB (5), TST (2), MVN (1) |
| Data processing, register | MOV (46), ORR (19), CMP (15), shifts by register (13), AND (8), BIC (6), ADD (4), SUB (3), ADC (2), MVN (1) |
| Load and store | STR imm (103), LDRB/STRB imm (21), LDR/STR reg (14), LDRH/STRH imm (11), LDM/STM/STMDB (15), PUSH (19), POP (16), LDRSB (1) |
| Branch | BL (36), CBZ (24), CBNZ (2) |
| SP arithmetic | ADD SP (4), SUB SP (1) |
| Multiply, divide, bit field, extend | UBFX (8), UXTB (7), UXTH (4), MUL (3), CLZ, BFC, BFI, UDIV (1 each) |
| M profile (this target) | MSR (16), MRS (11), IT (8), DSB (8), ISB (6), CPSID (5) |

The first six rows are common pseudo-code (athrill-arm-common); the last row
is the M profile's own and goes into this target.

### With the OS start: 110 instructions

Besides the above, `nx_start` reaches more of the same kinds (more encodings
of STR, LDRB/STRB, ADD/SUB register, STRD/LDRD, MLA, MLS, SDIV, UMULL, TBB,
SBFX, SXTB/SXTH, ...) and these, which need more than an instruction:

- SVC (7) and the context switch: the exception model (stacking, SVCall,
  PendSV, SysTick, EXC_RETURN);
- LDREX/STREX (20 each): the exclusive monitor;
- BLX (register, 61), DMB (34), CPSIE (1);
- floating point (VMRS, VLDR, VCMP, VMUL, VADD, VDIV, VCVT, ...: 47 uses),
  double precision as Cortex-M7's FPv5 has it.

## Beyond instructions

The start-up also needs, before any instruction is reached:

- the vector table at 0x08008000: the firmware sits after PX4's bootloader,
  so the reset reads it there (the reset address is fixed to 0x08000000 now);
- RCC and PWR ready flags: `stm32_clockconfig` waits on RCC_CR (HSIRDY,
  HSERDY, PLLRDY, PLLSAIRDY), RCC_CFGR (SWS) and PWR_CSR1 (ODRDY, ODSWRDY),
  forever for some of them, so these registers must answer as the hardware
  does once enabled;
- the system control space it writes: SCB (VTOR, CPACR, CCR, caches), MPU,
  SysTick, NVIC.

## Progress

Measured with `tools/coverage.sh` and `tools/encoding_coverage.py --usage`
(share of the start-up's instructions, counted by use):

| Date | Executors | Before the OS: with an executor / tested | With the OS start: with an executor / tested |
|---|---|---|---|
| 2026-11-03 | 63 encodings (ADD, SUB, CMP, AND, MOV, LDR, B, ...) | 37 of 93 encodings, 60.9% / 58.5% of uses | 50 of 184 encodings, 63.8% / 61.6% of uses |

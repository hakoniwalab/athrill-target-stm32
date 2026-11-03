# Testing

How the STM32 target is checked. The layers follow the RISC-V target, which
was checked with riscv-tests, NuttX ostest, trace comparison with another
implementation, and a decoder cross-check.

## 1. Toolchain (Docker)

All test programs and firmware are built in one Docker image,
[docker/Dockerfile](../docker/Dockerfile): PX4's own build image
(`px4io/px4-dev:v1.17.0-beta1`, with the Arm GNU toolchain `arm-none-eabi-gcc`
13.2 that PX4 uses for Pixhawk) plus the reference tools (QEMU's Cortex-M7
machine `mps2-an500`, llvm-objdump). PX4 `px4_fmu-v5`, NuttX for the ostest
and the hand-written tests all use the same compiler.

```bash
docker build -t athrill-stm32-toolchain docker/
```

Renode joins the image once its Linux package for the host architecture is
checked.

## 2. Instruction set

There is no freely usable official Armv7-M test suite (Arm's compliance kit
is a licensed product), so three kinds of tests are combined:

| Kind | Content | Reference |
|---|---|---|
| Self-checking programs | Per instruction class: results, flags (N, Z, C, V, Q, GE), exceptions; floating point with rounding modes and exception flags. The same form as the RISC-V target's self-tests | Arm's architecture reference manual |
| Random instruction sequences | Random Thumb-2 sequences run from the same initial state on Athrill and on the reference; registers and memory compared after each instruction | QEMU `mps2-an500` (Cortex-M7), used as an external tool |
| Decoder cross-check | Every 16-bit encoding and sampled 32-bit encodings disassembled and compared (design decision 10) | GNU objdump, llvm-objdump |

## 3. Exceptions and interrupts

Self-checking programs for the stacked frame on exception entry, priorities
and preemption, tail-chaining, lazy floating-point stacking, SysTick, PendSV,
SVCall and exception return (EXC_RETURN), compared with QEMU and Renode.

## 4. The whole chip: Renode

Renode models the STM32F7 with its peripherals. The same firmware runs on
Athrill and on Renode, and their behaviour is compared: peripheral register
sequences, NuttX and PX4 boot logs and, with execution traces, the
instruction stream up to the first difference. Renode is used as an external
tool only (design decision 8). The same runs also compare the speed of the
two.

## 5. Operating systems

- NuttX ostest on an STM32F7 board configuration of NuttX (for example
  Nucleo-F767ZI), with the NuttX version PX4 uses: runs to the end and passes.
- PX4 `px4_fmu-v5_default`, as built, boots to the NSH prompt (stage 1).
- ChibiOS tests with ArduPilot (stage 3).

## 6. Peripherals and sensor chips

Unit tests per peripheral against the STM32F7 reference manual: register
reads and writes, status bits, interrupts, DMA transfers. Sensor chips: the
registers their PX4 drivers use (identification, setup, data-ready, data).
The same form as the RISC-V target's UART and timer unit tests.

## 7. Rules carried over from the RISC-V target

- Every speed-up gives byte-identical output to one-instruction-at-a-time
  execution.
- Athrill can write an execution trace (with the virtual clock) for
  comparisons with other implementations.
- All tests run automatically and in CI.

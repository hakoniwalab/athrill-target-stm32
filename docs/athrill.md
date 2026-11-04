# Athrill: what it is, and how it differs from QEMU and Renode

## What Athrill is

[Athrill](https://github.com/toppers/athrill) is a CPU emulator of the
TOPPERS project, made to run and debug embedded programs (bare metal or on
a real-time OS) on a virtual microcontroller, first for the V850. The
current generation, `athrill3` (version 3, from v3.0.0), is a
CPU-independent core with one repository per target. Athrill supports four
instruction sets:

| Target | Instruction set | Runs |
| --- | --- | --- |
| [athrill-target-v850e2m](https://github.com/toppers/athrill-target-v850e2m) | V850E2M | TOPPERS real-time OS programs |
| [athrill-target-rh850f1x](https://github.com/toppers/athrill-target-rh850f1x) | RH850 (F1x) | programs for the RH850/F1x microcontrollers |
| [athrill-target-riscv](https://github.com/hakoniwalab/athrill-target-riscv) | RV64IMFC | PX4 on the AIFoundry Erbium board (`aifoundry_erbium_default`, a board in upstream PX4) |
| athrill-target-stm32 (this repository) | Armv7-M (Cortex-M7, FPv5-D16) | PX4 on Pixhawk 4 (`px4_fmu-v5_default`) |

The core is under the TOPPERS License; this target is under the MIT
licence (the Arm-generated decoders under Arm's BSD-3-Clause).

## Its characteristics

**An interpreter on a virtual clock.** Athrill interprets instructions
(no translation to host code), one virtual clock per instruction, with a
decode cache and batches of instructions between device updates. Devices
work out their state from the clock when they are accessed and tell the
core when their next event is, so idle time (PX4's idle loop, waiting for a
timer) is skipped rather than executed. Everything a run does is a function
of the program, its starting state and inputs given at virtual times: the
same flight from the same F-RAM and SD card images gives the same SD card
image, byte for byte ([verification](verification.md#determinism)).

**Instruction semantics you can check against Arm's manual.** The decoders
are generated from Arm's machine-readable architecture data (AARCHMRS) and
cross-checked against GNU objdump and llvm-objdump
([decoder](decoder.md)). The instructions execute through
[athrill-arm-common](https://github.com/hakoniwalab/athrill-arm-common),
which follows the Arm pseudo-code function by function, and every encoding
is tested against QEMU ([testing](testing.md)). Speed comes from two
options that keep the bits: fast executors for the frequent instructions
and the host FPU where IEEE 754 gives the same result, both checked
against the pseudo-code path and switchable off
(`DEVICE_CONFIG_ARM_EXEC_MODE` / `DEVICE_CONFIG_ARM_FP_MODE strict`). A whole
PX4 flight gives the same SD card image in both modes.

**Lightweight models of what the firmware uses.** Peripherals are
register-level functional models (timers, UARTs, SPI, I2C, DMA, SDMMC, ...)
written for what PX4 and NuttX do with them, not every feature of the
silicon. The emphasis is on the chips on the buses: the ICM-20689, MS5611,
IST8310, u-blox GPS, F-RAM and microSD card answer PX4's unmodified
drivers. Each model is a small C file with its unit test.

**Made to be connected.** External devices load as shared libraries
(`memory.txt` `DEV` lines), and Athrill can run as an asset of the
[Hakoniwa](https://github.com/toppers/hakoniwa) co-simulation environment,
advancing in lockstep with physics simulators such as MuJoCo; UARTs can be
TCP endpoints (QGroundControl).

**Debugging.** Athrill's interaction mode (`-i`) is a debugger for the
emulated program; a run can also be traced or PC-sampled
(`DEBUG_PC_SAMPLE_START`).

## Compared with QEMU and Renode

QEMU and Renode are mature, widely used emulators, with far broader
coverage than Athrill. This table states how they are built and what they
offer for this use, from their documentation; it is not a benchmark.

| | Athrill (this target) | QEMU | Renode |
| --- | --- | --- | --- |
| CPU execution | Interpreter with a decode cache | Dynamic binary translation (TCG) | Dynamic binary translation (tlib, derived from QEMU's translator) |
| Instruction semantics | Arm pseudo-code (athrill-arm-common), strict mode keeps to it alone | Hand-written translation per instruction | QEMU-derived translation |
| Scope | One MCU and board per target: Armv7-M, Pixhawk 4 | Many architectures and machines; no STM32F7 machine upstream (Cortex-M7 only in the MPS2-AN500 FPGA image model) | Many MCUs and boards from platform descriptions, including STM32 families; multi-node systems |
| Time and determinism | Virtual clock by design; identical runs shown for a PX4 flight | Wall-clock by default; instruction counting (`-icount`) and [record/replay](https://www.qemu.org/docs/master/system/replay.html) for reproducible runs | Virtual-time framework aimed at reproducible runs |
| Flight-controller use | PX4 on Pixhawk 4 (this repository) | No STM32F7 board model | [ArduPilot documents firmware emulation with Renode](https://ardupilot.org/dev/docs/renode-emulation.html), with its own SITL physics |
| Co-simulation | Hakoniwa asset (lockstep), external device libraries | Device models in QEMU, external links by sockets or chardevs | Robot Framework testing, sockets and external interfaces |
| Implementation | C, about one small file per device model | C, very large | C# (.NET) framework with a C CPU library |
| Licence | Core: TOPPERS License; target: MIT | GPL-2.0 | MIT |

What this comes to:

- **Use QEMU or Renode** for breadth: many boards and processors, speed
  from dynamic translation, multi-node systems, an established ecosystem.
  For ArduPilot on STM32, Renode is the documented path.
- **Athrill's place** is narrower: a PX4 Pixhawk 4 firmware path that is
  small enough to read, whose instruction semantics can be checked against
  Arm's pseudo-code, whose runs repeat byte for byte on a virtual clock,
  and which plugs into Hakoniwa. We do not claim it is faster than QEMU or
  Renode; it runs PX4 SIH at about twice real time on an Apple M2 Pro
  ([verification](verification.md#speed)).

Athrill takes no code from QEMU or Renode ([design](design.md), decision 8).
QEMU is used as a reference only: the instruction tests compare Athrill's
output with the same programs on QEMU.

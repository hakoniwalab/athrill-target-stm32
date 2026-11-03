# Survey (stage 0)

What exists for running Pixhawk firmware on an emulated STM32, and what it
means for this target. Checked on 2026-11-03.

## 1. ArduPilot on Renode

ArduPilot documents Renode as a developer tool
([Renode MCU Emulation](https://ardupilot.org/dev/docs/renode-emulation.html),
[ArduPilot/renode](https://github.com/ArduPilot/renode)):

- Unmodified ArduPilot STM32 firmware (the same ELF/APJ as flashed to a board)
  runs on an emulated MCU; the real ChibiOS build, drivers, DMA and bus
  handling run, unlike in SITL.
- Support is per MCU family (STM32 F1, F3, F4, F7, G4, H7, L4). Renode board
  descriptions are generated from ArduPilot's own `hwdef.dat` at launch, with
  one hand-maintained base platform per MCU family.
- Representative models: IMU, barometer, compass, GPS, airspeed, rangefinder,
  battery, IOMCU (a fast synthetic one by default; the real STM32F100 IOMCU
  firmware can run in a second machine), FRAM, dataflash, SD card.
- Flight dynamics: a separate physics process reusing ArduPilot's SITL vehicle
  models, exchanging actuator and sensor state with Renode over a localhost
  lockstep protocol.
- A GitHub Actions job flies a CubeOrangePlus quadplane mission against it.
- Limits stated there: peripheral models are functional, not electrically or
  physically accurate; execution is paced to wall-clock time by default
  (`--unthrottled` removes it); a patched Renode build is recommended for speed.

## 2. PX4 on Renode or other MCU emulators

No published work running PX4 on Renode or another MCU emulator was found.
PX4's simulation documentation covers SITL (Gazebo and others) and HITL with a
real board ([PX4 developer environment](https://docs.px4.io/v1.13/en/dev_setup/dev_env)).

## 3. QEMU

QEMU has no STM32F7 machine. Cortex-M7 is available only in Arm's MPS2 FPGA
image model `mps2-an500`
([QEMU MPS2](https://qemu.googlesource.com/qemu/+/e5c31ef558da16fb9cbb9e3a3c7247b60fdfd05b/docs/system/arm/mps2.rst),
[qemu-arm list](https://lists.libreplanet.org/archive/html/qemu-arm/2023-06/msg01046.html),
[no STM32F746 machine](https://sorhanp.gitlab.io/posts/embedded-part6-qemu)).
Useful at most as a reference for CPU behaviour.

## 4. Pixhawk 4 (FMUv5) in PX4

From PX4's board definition (`boards/px4/fmu-v5`, `init/rc.board_sensors`,
`default.px4board`) and
[the NuttX 2019 talk](https://events.nuttx.apache.org/wp-content/uploads/2019/11/DSidrane_nx2019.pdf)
(FMUv5 is STM32F765 with an STM32F100 I/O coprocessor):

| Kind | Chips | Bus |
|---|---|---|
| IMU | ICM-20602 or ICM-42688-P (by board version), ICM-20689, BMI055 (accelerometer and gyroscope) | SPI |
| Barometer | MS5611 | SPI |
| Magnetometer | IST8310 (internal and external) | I2C |
| GPS | u-blox and others (`gps` driver) | UART |

- PX4 starts every listed sensor driver; a driver whose chip does not answer
  fails to start and is skipped. One IMU (for example ICM-20689), MS5611 and
  IST8310 are expected to be enough to fly.
- The main outputs come from the I/O coprocessor (`px4io`, STM32F100) over a
  UART; the STM32F765's own timers drive the AUX outputs. Ways to take the
  motor outputs: assign the motors to AUX outputs (parameters only), model the
  px4io protocol (registers over the UART), or run the coprocessor's firmware
  as a second emulated MCU.

## 5. Arm instruction set data

- Arm publishes machine-readable architecture data for the A-profile under a
  BSD licence (AARCHMRS, JSON; instruction sets A64, A32 and T32, system
  registers), as a subset of the proprietary-licensed packages
  ([AARCHMRS mirror](https://kernel.googlesource.com/pub/scm/linux/kernel/git/maz/AARCHMRS/+/HEAD)).
- An M-profile (Armv8-M) XML/ASL release was published in 2017
  ([Arm's machine-readable release](https://alastairreid.github.io/ARM-v8a-xml-release)).
  Whether a current M-profile package exists, and its terms, is to be checked
  on Arm's site.
- Thumb-2 encodings of the A-profile T32 data overlap the Armv7-M instruction
  set, but the M-profile differs (exception model, system instructions, no ARM
  state); differences would be added by hand.

## 6. Sources for the instruction decoder

As in the RISC-V target (tables generated from `riscv-opcodes`, cross-checked
against GNU objdump), the decoder tables should come from a machine-readable
source, with an independent decoder as the oracle.

| Source | What it gives | Licence | Use |
|---|---|---|---|
| Arm AARCHMRS open-source package (`Instructions.json`) | A64, A32 and T32 encodings from Arm's own data (T32 from the 2025-06 releases on) ([mirror](https://kernel.googlesource.com/pub/scm/linux/kernel/git/maz/AARCHMRS/+/HEAD), [aarchmrs-instructions crate](https://docs.rs/crate/aarchmrs-instructions/latest/source/README.md)) | BSD-3-Clause (Arm Limited), as stated by the crate generated from it; the package's own notice is to be read | Candidate main source for T32 encodings. It is the A-profile T32: Armv7-M is a subset with differences (no ARM state, M-profile system instructions `MRS`/`MSR`/`CPS` encodings, exception return), checked by hand |
| Arm Armv8-M machine-readable specification (XML/ASL) | Encodings and executable ASL of the M-profile | Released in 2017 per its authors ([FMCAD 2016](https://alastairreid.github.io/papers/FMCAD_16), [v8-M validation](https://alastairreid.github.io/validating-specs)); no current package or terms were found | Check Arm's site; if available on usable terms, the best match for M-profile |
| ASLi, the ASL interpreter ([asl-interpreter](https://github.com/alastairreid/asl-interpreter), [opam asli](https://ocaml.org/p/asli)) | Runs Arm's ASL | BSD-3-Clause (the tool; Arm's ASL keeps Arm's terms) | Possible behaviour oracle, if an M-profile ASL is available |
| Sail ([Sail licence](https://ocaml.org/p/sail/0.16/LICENSE.html)) | Sail tool and models; the Arm models are A-profile, with the ASL-derived parts copyright Arm | BSD-2-Clause except the ASL-derived parts | No M-profile model found; not a source here |
| LLVM `ARMInstrThumb2.td` ([source](https://grok.dragonflybsd.org/xref/freebsd/contrib/llvm-project/llvm/lib/Target/ARM/ARMInstrThumb2.td?r=9768746b)) | Thumb-2 encodings as TableGen records, with M-profile predicates | Apache-2.0 with LLVM exception | Usable as an oracle (llvm-objdump / llvm-mc); not copied |
| GNU objdump, Capstone | Disassemblers | GPL (binutils), BSD-3-Clause (Capstone) | Oracles for cross-checking every 16-bit encoding and sampled 32-bit encodings, as GNU objdump is for the RISC-V target; no code copied |

## 7. Consequences for the design

- ArduPilot on STM32 already has an emulator path (Renode). Starting with PX4,
  as decided, is where this target adds something.
- Model three sensors at first (one IMU, MS5611, IST8310) and a GPS.
- The motor-output path (AUX outputs or a px4io model) is to be decided.
- The instruction decoder: generate the tables from Arm's AARCHMRS T32 data
  (after reading the package's licence notice) restricted to Armv7-M, unless
  a current Armv8-M package is available on usable terms; cross-check with GNU
  objdump and llvm-objdump.

## 8. Next: Renode's execution engine (JIT)

Renode's CPU emulation translates guest code to host code (tlib, derived
from QEMU's translator), while Athrill interprets with a decode cache and
straight-line blocks. Using Renode as the reference (testing, 4) also gives:

- Speed: the same STM32F7 firmware on both, to see what dynamic translation
  gains over Athrill's interpreter for this kind of firmware.
- Where time goes: ArduPilot publishes its Renode performance patches and
  analysis (`Tools/renode/README.md` in ArduPilot, the performance branch of
  [ArduPilot/renode](https://github.com/ArduPilot/renode)). Whether device
  accesses, interrupts and time synchronisation dominate there too, as
  device accesses ending a batch do in Athrill's RISC-V target.
- Determinism: how Renode keeps virtual time and the points where interrupts
  are taken with translated blocks, as input for a JIT in Athrill that keeps
  the rule of byte-identical output to one-instruction-at-a-time execution
  (design decision 7).

Only published documents, performance analyses and observed behaviour are
used; no code is taken from Renode or QEMU (design decision 8).


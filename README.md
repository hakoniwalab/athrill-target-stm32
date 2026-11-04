# Athrill STM32 — run PX4's Pixhawk 4 firmware without a flight controller

**Run a board-target PX4 firmware build on a virtual Pixhawk 4 (FMUv5), connect QGroundControl, and take off without a physical flight controller.**

PX4's Software-in-the-Loop (SITL) is an excellent way to develop and test flight
software. But what if we also want to run the firmware built for an actual
flight-controller board, including NuttX, its interrupt handling, and STM32
peripheral drivers, **before putting it on hardware**?

This project explores **Processor-in-the-Loop (PIL)** as a complementary testing
option. It extends the open-source [Athrill](https://github.com/toppers/athrill)
microcontroller emulator with an Armv7-M (Cortex-M7) target and functional
STM32F7 / Pixhawk 4 devices. The result runs the regular PX4
`px4_fmu-v5_default.elf` build on a development computer, rather than a
host-native `px4_sitl` build.

We started with the established **Pixhawk 4 / FMUv5** platform
(STM32F765, 216 MHz), so PX4 developers can try a recognizable
board-target configuration. This is a lightweight *functional* emulator,
not a cycle-accurate or electrically accurate substitute for real hardware.

## What works today

Two different flight paths have been demonstrated. They exercise different
parts of PX4 and should not be confused:

| Demonstration | Where physics runs | What is exercised | Observed result |
| --- | --- | --- | --- |
| **PX4 SIH on emulated Pixhawk 4** | Inside PX4 (`simulator_sih`; `SYS_HITL=2`) | Board-target PX4 firmware, NuttX, virtual MCU, MAVLink, persistent storage | QGroundControl takeoff and hover at **2.49–2.51 m**; ULog saved on emulated microSD |
| **PX4 + Hakoniwa Drone PRO** | Outside PX4, in MuJoCo through Hakoniwa | The same board-target firmware **and PX4's own sensor drivers**, communicating with emulated chips; AUX PWM motor outputs | x500 takeoff and hover at **2.31–2.33 m** relative altitude (2.5 m target; not yet investigated) |

For the first experiment, SIH generates sensor data internally in PX4;
**the emulated physical sensor chips are not in the SIH data path**. The
Hakoniwa experiment separately tests those chip models and the PX4 drivers.

**Reproduction guides:**
- [PX4 SIH on Athrill: QGroundControl, configuration, and microSD logging](docs/sih.md)
- [PX4 with Hakoniwa Drone PRO: emulated sensors, AUX PWM, and x500](docs/hakoniwa.md)

Hakoniwa Drone PRO is a separate simulation component; **it is not needed for
the SIH demonstration**.

## Why PIL in addition to SITL and HITL?

| Approach | Firmware execution | Physical flight controller |
| --- | --- | --- |
| **SITL** | Host-built PX4 with simulation interfaces | Not required |
| **HITL / SIH on hardware** | Board-target PX4 on a real flight controller | Required |
| **Athrill PIL + SIH** | Board-target PX4 and NuttX on an emulated MCU; PX4's internal SIH physics | **Not required** |
| **Athrill PIL + external physics** | Board-target PX4 and NuttX, with emulated device interfaces and external dynamics | **Not required** |

The intended use is to explore firmware bring-up, board-specific integration,
driver behavior, and reproducible regression scenarios without having a Pixhawk
connected for every test. PIL complements rather than replaces SITL or tests on
real hardware; these are potential uses, not claims of complete verification
coverage.

## Emulated hardware

The target models the parts of the **Pixhawk 4 / STM32F765** that the demonstrated
firmware paths need:

| Area | Current implementation |
| --- | --- |
| CPU | Cortex-M7 / Armv7-M Thumb-2, DSP, FPv5-D16; NVIC, SysTick, exceptions and context switching |
| Board initialization | Lightweight RCC / clock / GPIO and board support |
| Communications | STM32 UART/USART, baud-timed RX/TX, DMA and UART-to-localhost-TCP bridge for MAVLink / QGroundControl |
| Buses and timing | SPI, I2C, DMA, EXTI, timers and FMU AUX PWM outputs |
| Parameters | FM25V02A F-RAM on SPI, optionally persisted to a host file |
| Flight logging | microSD on SDMMC1, including DMA transfer and a host-backed FAT32 image with PX4 ULog files |
| Sensor chips | ICM-20689 IMU (SPI1 / DMA / 8 kHz data-ready), MS5611 barometer (SPI4), IST8310 magnetometer (I2C3), u-blox M8 GPS (USART1) |

These are *functional register-level models*: they implement the interactions
the tested firmware uses, not every feature of the silicon. The emulated
Pixhawk 4's separate PX4IO co-processor and USB are not currently modeled.
For the external-physics demonstration, motor outputs are assigned to FMU
AUX1–AUX4 rather than PX4IO's MAIN outputs.

## Getting started

### 1. Build Athrill STM32

Place three repositories side by side. The core currently uses its `cpp`
branch:

```sh
git clone --branch cpp https://github.com/toppers/athrill.git
git clone https://github.com/hakoniwalab/athrill-arm-common.git
git clone https://github.com/hakoniwalab/athrill-target-stm32.git
cd athrill-target-stm32

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
(cd build && ctest --output-on-failure)
```

As a small smoke test, run the included bare-metal example:

```sh
./build/athrill3 -c1 -m test/hello/memory.txt -d test/hello/device_config.txt -t 100000 test/hello/hello.elf
```

Expected console output includes `Hello, Athrill STM32!`.

### 2. Run PX4 SIH

Build (or provide) the upstream PX4 **`px4_fmu-v5_default` ELF** for Pixhawk 4.
This is the firmware target used in the experiments; `px4_sitl` is not a
substitute. Follow [docs/sih.md](docs/sih.md) for the memory map, device
configuration, persistent F-RAM, optional FAT32 microSD image, and the
`SYS_AUTOSTART=1100` / `SYS_HITL=2` SIH setup.

The documented run command, with those files and the firmware ELF prepared,
is:

```sh
./build/athrill3 -c1 -m memory.txt -d device_config.txt -t 60000000000 px4_fmu-v5_default.elf
```

The sample configuration exposes PX4 TELEM1 over TCP at
`127.0.0.1:5760`. Connect **QGroundControl** using a TCP communication link
(or pymavlink using `tcp:127.0.0.1:5760`), then use the normal arm/takeoff
flow. The emulated SD card can be mounted after the run to inspect the ULog.

The command above assumes you have created the configuration files and
supplied the ELF; it is **not yet a one-command installer**.

### 3. Run with external physics (optional)

[docs/hakoniwa.md](docs/hakoniwa.md) describes the separate setup using
Hakoniwa Drone PRO / MuJoCo. The emulated IMU, barometer, magnetometer, and
GPS feed PX4's normal drivers; PX4's FMU PWM signals drive Hakoniwa's x500.

The documented flight tests were run on an **Apple Silicon Mac**. We would
also like to provide a verified **Windows/WSL2** walkthrough; this has not
yet been documented or validated here.

## Performance and validation

Results from **2026-11-04**, on an Apple Silicon Mac, with virtual STM32
clock at 216 MHz:

| Workload | Real-time factor (RTF) |
| --- | ---: |
| PX4 SIH hover, QGroundControl/MAVLink and SD logging, after optimization | **1.94** |
| PX4 with Hakoniwa Drone PRO / MuJoCo, emulated chip drivers, x500 hover, no viewer | **1.48** |

An RTF above 1 means virtual simulation time advances faster than elapsed
wall-clock time for the measured workload. These are *workload- and
host-specific measurements*, not a CPU clock-accuracy result or a comparative
benchmark against other emulators. See [performance notes](docs/performance.md).

Athrill's instruction decoders are generated from Arm machine-readable
architecture data. The Arm instruction execution path uses shared
[athrill-arm-common](https://github.com/hakoniwalab/athrill-arm-common)
pseudo-code; optional fast executors are compared against that reference
path. Instruction, FP, and exception tests are compared with QEMU on suitable
Arm targets. See [testing](docs/testing.md) and [decoder details](docs/decoder.md).

A virtual-clock-based event model and Hakoniwa lockstep integration are
central design choices. However, **complete deterministic replay across all
external inputs and all configurations has not been established**. The
emulation is neither instruction-cycle accurate nor suitable as a substitute
for board-level electrical, safety, or flight validation.

## Related work and motivation

This is not a claim that MCU emulation is new, or that other emulators cannot
run deterministically. For example, [QEMU supports instruction counting
and record/replay](https://www.qemu.org/docs/master/system/replay.html), and
[ArduPilot documents firmware emulation with
Renode](https://ardupilot.org/dev/docs/renode-emulation.html).

Our focus is narrower: a **lightweight, inspectable PX4 FMUv5 firmware
execution path** that can be connected to the Hakoniwa simulation environment,
with explicit virtual time and testable functional peripheral models. We do
not yet claim a measured speed advantage over QEMU or Renode.

An earlier [Athrill RISC-V target](https://github.com/hakoniwalab/athrill-target-riscv)
runs PX4 on the AIFoundry Erbium board, including an optional MAVLink
configuration for QGroundControl. Starting with the familiar **Pixhawk 4 /
FMUv5** makes this work easier to evaluate for developers using STM32-based
PX4 boards.

Feedback from PX4 developers on useful test cases, missing hardware models,
reproduction steps, and limitations is very welcome.

## Design and license

- [Design decisions and staged goals](docs/design.md)
- [Survey of existing approaches](docs/survey.md)
- [Test strategy](docs/testing.md)
- [Performance measurements](docs/performance.md)

`src/cpu/decoder/t32_decode16.{h,c}` and `t32_decode32.{h,c}` are generated
from Arm's Architecture Machine Readable Specification and are provided under
Arm's BSD 3-clause licence, reproduced in those files (Copyright 2010–2026
Arm Limited or its affiliates). The rest of this repository is under the
[MIT licence](LICENSE).

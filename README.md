# Athrill STM32 — unmodified STM32 firmware, without the board

[![ctest](https://github.com/hakoniwalab/athrill-target-stm32/actions/workflows/ctest.yml/badge.svg)](https://github.com/hakoniwalab/athrill-target-stm32/actions/workflows/ctest.yml)
[![px4](https://github.com/hakoniwalab/athrill-target-stm32/actions/workflows/px4.yml/badge.svg)](https://github.com/hakoniwalab/athrill-target-stm32/actions/workflows/px4.yml)

**PX4, ArduPilot and Zephyr, built for real STM32F7 and STM32H7 boards, run unchanged on your computer. The boards
are built from their Devicetree, and every run driven by virtual time repeats bit for bit.**

![QGroundControl (left) takes off and flies "Go to location" legs with PX4 SIH on the emulated Pixhawk 4; PX4's own console (right) reports PX4 v1.17.0 on an STM32F76x, the arming, the ULog file and the takeoff. 4x speed.](docs/qgc-flight.gif)

*QGroundControl flying PX4 v1.17.0 on the emulated Pixhawk 4, with PX4's console on the right (4× speed).*

This is the Armv7-M (Cortex-M7) target of [Athrill](https://github.com/toppers/athrill), a lightweight
microcontroller emulator with an exact virtual clock. The firmware is the file you would flash: the same ELF,
with its RTOS, interrupts, DMA and drivers. Nothing is rebuilt for the host.

## What runs

| Firmware | Board (SoC) | What is shown |
| --- | --- | --- |
| PX4 v1.17.0 `px4_fmu-v5_default` | Pixhawk 4 (STM32F765) | SIH flight from QGroundControl; PX4's own drivers on the emulated sensor chips; the Hakoniwa Drone x500 in MuJoCo, flown by those drivers |
| PX4 v1.17.0 `px4_fmu-v6c_default` | Pixhawk 6C (STM32H743) | arms and flies the x500 in Hakoniwa |
| ArduCopter V4.7.2, the official `Pixhawk4` binary | Pixhawk 4 | boots with its own drivers, flies the x500; faster than ArduPilot's Renode setup |
| ArduCopter V4.7.2 `Pixhawk6C` | Pixhawk 6C | flies the x500 in Hakoniwa |
| Zephyr samples, built for Zephyr's `nucleo_f767zi` and `nucleo_h743zi` | NUCLEO-F767ZI, NUCLEO-H743ZI | `hello_world`, `synchronization`, `philosophers` |

The same Athrill core also runs PX4 and Zephyr on RISC-V: see
[Athrill RISC-V](https://github.com/hakoniwalab/athrill-target-riscv).

## Boards from Devicetree

There is no board code in this target. A board is a Devicetree blob from
[athrill-device](https://github.com/hakoniwalab/athrill-device), and the blob names the models Athrill builds: the
STM32 USART, SPI, I2C, DMA, timers and PWM, GPIO, EXTI, SDMMC, and the sensor chips on their buses.

- The SoC description is Zephyr's own `stm32f765Xi.dtsi` or `stm32h743Xi.dtsi`, included unchanged.
- The Nucleo boards include Zephyr's whole board file, `nucleo_f767zi.dts`. A Zephyr application built for that
  board and the board Athrill emulates come from one description.
- A node that is enabled but has no model stops Athrill at start with its path. Nothing is silently left out.
- The Pixhawk boards have regression references (instruction count, console, F-RAM, SD card image). CI compares
  Pixhawk 4's on every change.

To add a board, see
[Adding a board from a Devicetree](https://github.com/hakoniwalab/athrill-device/blob/main/docs/adding-a-board.md).

## Try it in two minutes

The release has `athrill3` for macOS (arm64) and Linux (x86_64), the PX4 ELF and the Zephyr sample ELFs:

```sh
curl -LO https://github.com/hakoniwalab/athrill-target-stm32/releases/download/v0.3.0/athrill3-v0.3.0-macos-arm64.tar.gz
curl -LO https://github.com/hakoniwalab/athrill-target-stm32/releases/download/v0.3.0/zephyr-samples-v0.3.0.tar.gz
curl -LO https://github.com/hakoniwalab/athrill-target-stm32/releases/download/v0.3.0/SHA256SUMS
shasum -a 256 -c --ignore-missing SHA256SUMS
tar xzf athrill3-v0.3.0-macos-arm64.tar.gz
tar xzf zephyr-samples-v0.3.0.tar.gz
git clone --branch v0.3.0 https://github.com/hakoniwalab/athrill-target-stm32.git
git clone https://github.com/hakoniwalab/athrill-device.git
git -C athrill-device checkout ff23daf
athrill-target-stm32/examples/zephyr/run.sh nucleo_f767zi zephyr-samples-v0.3.0/philosophers-nucleo_f767zi.elf 10 athrill3-v0.3.0-macos-arm64/athrill3
```

On Linux, use `athrill3-v0.3.0-linux-x86_64.tar.gz` and `sha256sum -c --ignore-missing SHA256SUMS`.
[examples/zephyr](examples/zephyr) checks all six Zephyr runs and builds the ELFs yourself.

For PX4 SIH with QGroundControl, also download `px4_fmu-v5_default.elf` and run

```sh
athrill-target-stm32/examples/px4-sih/run.sh px4_fmu-v5_default.elf athrill3-v0.3.0-macos-arm64/athrill3
```

then add a TCP link to `127.0.0.1:5760` in QGroundControl, arm and take off. The vehicle starts at PX4's SIH home
(Zurich). [docs/sih.md](docs/sih.md) explains each setting. QGroundControl warns that HITL is enabled
(`SYS_HITL=2` is how SIH runs) and asks for radio setup: both are expected.

For ArduPilot, take the official binary from ArduPilot's firmware server. ArduPilot is GPL-3.0, so this project
does not redistribute it; the version directory and the SHA-256 pin the file the results come from (ArduCopter
V4.7.2 for the Pixhawk 4):

```sh
curl -LO https://firmware.ardupilot.org/Copter/stable-4.7.2/Pixhawk4/arducopter.elf
shasum -a 256 arducopter.elf   # cfa1b3f04f326ab91760c53b93503b669d4d62e2d43dd1ffc4d09cb15198eb2c
athrill-target-stm32/examples/ardupilot-bench/run.sh arducopter.elf 4320000000 athrill3-v0.3.0-macos-arm64/athrill3
python3 athrill-target-stm32/examples/ardupilot-bench/check-mavlink.py athrill-target-stm32/examples/ardupilot-bench/mavlink.raw
```

ArduCopter boots with its own drivers for 20 virtual seconds and ends with `steps=722036516` and
`FINAL pc=0x08138a48`, the same on every run. `check-mavlink.py` (needs pymavlink) reports 23 HEARTBEAT messages
on TELEM1. [examples/ardupilot-bench](examples/ardupilot-bench) and [docs/ardupilot.md](docs/ardupilot.md) have
more, and [docs/hakoniwa.md](docs/hakoniwa.md#ardupilot) flies it.

## Fly in Hakoniwa (Processor-in-the-Loop)

The firmware's own drivers read the emulated IMU, barometer, magnetometer and GPS chips. Their samples come from
the [Hakoniwa Drone](https://github.com/toppers/hakoniwa-drone-core) x500 in MuJoCo. The firmware's PWM outputs go
back as motor commands, in lockstep. Two ways to connect:

- **Hakoniwa link**: Athrill and the Hakoniwa Drone aircraft service run as two Hakoniwa assets. Other Hakoniwa
  assets and viewers can join.
- **In process**: the aircraft runs inside Athrill, so one process steps the firmware and the vehicle. Good for
  firmware tests, tuning and regression runs.

Measured on an Apple M2 Pro, no viewer, x500 take-off to 2.5 m and hover, 2026-11-07 and 2026-11-08:

| Firmware on board | Runs | Real-time factor |
| --- | --- | ---: |
| PX4, Pixhawk 4, Hakoniwa link | 3/3 hold 2.5 m | 1.61–1.65 |
| ArduCopter, Pixhawk 4, Hakoniwa link | 3/3 hold 2.5 m | 0.99–1.09 |
| PX4, Pixhawk 4, in process | 3/3 hold 2.5 m | 1.81–1.85 |
| ArduCopter, Pixhawk 4, in process | 3/3 hold 2.5 m | 1.18–1.23 |
| PX4, Pixhawk 6C (480 MHz), in process | 2/2 hold 2.5 m | 1.28 |
| ArduCopter, Pixhawk 6C, in process | 2/2 hold 2.5 m | 0.74–0.76 |

[docs/hakoniwa.md](docs/hakoniwa.md) has the setup, with the free edition of Hakoniwa Drone v5.0.0. The sensor
device and both links are in athrill-device
([in-process aircraft](https://github.com/hakoniwalab/athrill-device/blob/main/docs/in-process-aircraft.md)).

## ArduPilot, and a race with Renode

ArduPilot also runs this firmware on Renode, so we compared the two on one Apple M2 Pro with the same ELF:

| | Real-time factor (mean of 3) |
| --- | ---: |
| **Athrill STM32**, a branch to itself waits as `WFI` (no knowledge of the firmware) | **1.583** |
| Athrill STM32, the idle loop executed as written | 0.62 |
| Renode, ArduPilot's performance branch | 0.30 |

The PC traces of the two emulators agree on 411,627 of the 411,873 instructions that Renode traced from reset;
they differ only where the firmware waits on hardware timing. Athrill is faster because it executes no idle time
(ArduCopter's idle loop `b .` halts until the next interrupt, and the clock jumps there), and because a device
computes its state from the clock when the firmware reads it. The emulated peripherals differ, so these are
results for these two configurations, not a normalised comparison.
[docs/ardupilot.md](docs/ardupilot.md) and [docs/time-skip.md](docs/time-skip.md) have the details.

## Bit-identical, and checked

[examples/px4-sih/verify](examples/px4-sih/verify) flies PX4 SIH end to end with one command (about 2 minutes):

- take off, hover and land three times from the NSH console at fixed virtual times. Two runs use the fast mode and
  one the strict mode (Arm pseudo-code only, no host FPU). The F-RAM, the **whole SD card image including the
  ULog** and the console must be identical;
- that flight's ULog: EKF2 against SIH's ground truth, attitude tracking, altitude and position hold;
- a QGroundControl-style MAVLink flight (arm, take off, go to 5 m north, land), checked by its log;
- with the release ELF, the SD card image, ULog, F-RAM and console equal to the published run, on any host.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/verification/altitude-dark.png">
  <img alt="Altitude over time: setpoint, SIH ground truth and EKF2 estimate within about 0.1 m of each other from takeoff through the 2.5 m hover to touchdown" src="docs/verification/altitude-light.png" width="720">
</picture>

CI boots the release PX4 ELF on every change and compares it with the board's regression reference. The thresholds
and what they do not show are in [docs/verification.md](docs/verification.md). Input timed by the wall clock, such
as QGroundControl's commands over TCP, arrives at run-dependent virtual times. Such runs are checked by their logs,
not for identity.

On the Mac (M2 Pro) PX4 v1.17.0 SIH hovered at RTF 2.22 with QGroundControl and SD logging (2026-11-04)
([docs/performance.md](docs/performance.md)).

## Debugging with GDB or LLDB

`athrill3 -g<port>` starts a GDB remote server. Break in a PX4, ArduPilot or Zephyr driver, step, read the
backtrace, registers (FPU included) and memory, from GDB, LLDB or VS Code. [docs/debugging.md](docs/debugging.md)
has the steps.

## Build from source

Place four repositories side by side, at the versions these results come from:

```sh
git clone https://github.com/toppers/athrill.git
git -C athrill checkout bc8d76a
git clone https://github.com/hakoniwalab/athrill-arm-common.git
git -C athrill-arm-common checkout 60d8098
git clone https://github.com/hakoniwalab/athrill-device.git
git -C athrill-device checkout ff23daf
git clone https://github.com/hakoniwalab/athrill-target-stm32.git
cd athrill-target-stm32

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
(cd build && ctest --output-on-failure)
```

A bare-metal smoke test, with the configuration the build writes (`test/hello/device_config.txt` and the
Pixhawk 4 Devicetree, for its USART3):

```sh
./build/athrill3 -c1 -m test/hello/memory.txt -d build/test-hello-config.txt -t 100000 test/hello/hello.elf
```

It prints `Hello, Athrill STM32!` and stops at its clock limit (`EXIT for timeout(100000).`, exit status 1).

PX4 itself, the stock target, with PX4's build image (about 6 minutes):

```sh
cd ..
git clone --branch v1.17.0 --depth 1 https://github.com/PX4/PX4-Autopilot.git PX4
docker run --rm -v "$PWD/PX4:/src" -w /src px4io/px4-dev:v1.17.0-beta1 make px4_fmu-v5_default
```

Athrill loads the **ELF** (it needs the symbols), not the `.px4` upload file, and starts PX4 at `0x08008000`, past
the bootloader. PX4 `main` works too ([docs/verification.md](docs/verification.md)). The steps were run as written
on an Apple Silicon Mac and on Windows 11 / WSL2 Ubuntu.

## The emulated hardware

| Area | Implementation |
| --- | --- |
| CPU | Cortex-M7 / Armv7-M Thumb-2, DSP, FPv5-D16; NVIC (lines from the Devicetree), SysTick, exceptions |
| Instructions | decoders generated from Arm's machine-readable architecture data; execution in shared Arm pseudo-code ([athrill-arm-common](https://github.com/hakoniwalab/athrill-arm-common)), with fast paths checked against it bit for bit; compared with QEMU |
| Communications | USART/UART (interrupt, DMA and polling receive), with a localhost TCP bridge for MAVLink |
| Buses and timing | SPI (F7 and the H7 SPI v2), I2C, DMA and DMAMUX, EXTI, timers and PWM outputs |
| Storage | FM25V02A F-RAM on SPI, microSD on SDMMC with DMA, both backed by host files |
| Sensor chips | ICM-20689, ICM-42688-P, BMI055, MS5611, IST8310, u-blox M8 |

These are functional register-level models of what the firmware uses, with an exact virtual clock (one instruction
is one clock), not cycle- or electrically accurate substitutes for the silicon. What a board does not model is listed
in its Devicetree; on the Pixhawk boards the PX4IO co-processor and USB are not modelled. The emulation is not a
substitute for board-level electrical, safety or flight validation.

## Related work

MCU emulation is not new. [QEMU](https://www.qemu.org/docs/master/system/replay.html) counts instructions and can
record and replay, and [Renode](https://ardupilot.org/dev/docs/renode-emulation.html) models the STM32F7 for
ArduPilot and, since 2026-09, flies it with ArduPilot's SITL physics. Athrill's differences:

- unmodified firmware of three kinds (PX4, ArduPilot, Zephyr) on boards built from Zephyr's Devicetree;
- external physics (Hakoniwa, MuJoCo), faster than real time;
- decoders generated from Arm's data, and a strict mode that a whole PX4 flight matches bit for bit.

[docs/athrill.md](docs/athrill.md) explains Athrill and how it differs from QEMU and Renode, and
[docs/related-work.md](docs/related-work.md) compares the ArduPilot setups. Against Renode we have the one measured
comparison above; against QEMU, none. Feedback on test cases, missing models and limitations is welcome.

## Documents and license

- [Configuration reference](docs/configuration.md): every STM32 target parameter
- [Athrill, and how it differs from QEMU and Renode](docs/athrill.md)
- [Design decisions](docs/design.md), [survey of existing approaches](docs/survey.md)
- [Testing](docs/testing.md), [verification](docs/verification.md), [performance](docs/performance.md)
- [ArduPilot and Renode](docs/ardupilot.md), [virtual time and the clock skip](docs/time-skip.md)

`src/cpu/decoder/t32_decode16.{h,c}` and `t32_decode32.{h,c}` are generated from Arm's Architecture Machine Readable
Specification and are provided under Arm's BSD 3-clause licence, reproduced in those files (Copyright 2010–2026 Arm
Limited or its affiliates). The rest of this repository is under the [MIT licence](LICENSE). The Zephyr sample ELFs
of the release carry their own licenses ([examples/zephyr](examples/zephyr#licenses-of-the-elfs)).

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

The stock PX4 `px4_fmu-v5_default` firmware boots on the emulated Pixhawk 4,
runs PX4's **SIH** (simulation-in-hardware) quadcopter, talks MAVLink to
QGroundControl over TCP, takes off to the default **2.5 m** and holds
**2.49–2.51 m**, and writes its ULog to the emulated microSD card, which can
be mounted on the host afterwards. Everything used is public: Athrill, this
target, PX4 itself and QGroundControl.

In SIH, PX4 computes the vehicle and its sensor data internally
(`SYS_HITL=2`); **the emulated sensor chips are not in the SIH data path**.
Flying with PX4's own sensor drivers on those chips is a second, separate
experiment (see [Beyond SIH](#beyond-sih-px4s-sensor-drivers-and-external-physics)).

## Why PIL in addition to SITL and HITL?

| Approach | Firmware execution | Physical flight controller |
| --- | --- | --- |
| **SITL** | Host-built PX4 with simulation interfaces | Not required |
| **HITL / SIH on hardware** | Board-target PX4 on a real flight controller | Required |
| **Athrill PIL + SIH** | Board-target PX4 and NuttX on an emulated MCU; PX4's internal SIH physics | **Not required** |

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

Place three repositories side by side, at the commits these results come
from (the core's `cpp` branch and athrill-arm-common move on):

```sh
git clone --branch cpp https://github.com/toppers/athrill.git
git -C athrill checkout ba809715
git clone https://github.com/hakoniwalab/athrill-arm-common.git
git -C athrill-arm-common checkout d5e5ef8
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

Expected console output includes `Hello, Athrill STM32!`; the run then
stops at its clock limit (`EXIT for timeout(100000).`, exit status 1).

### 2. Build the PX4 firmware

The stock Pixhawk 4 target of PX4 `main`, unmodified, at the commit the
demonstrations used, with PX4's build image (Docker), beside the other
repositories:

```sh
cd ..
git clone https://github.com/PX4/PX4-Autopilot.git PX4
git -C PX4 checkout 216039a12bab5a86769128f7be3e5f63dc7b4952
git -C PX4 submodule update --init --recursive
docker run --rm -v "$PWD/PX4:/src" -w /src px4io/px4-dev:v1.17.0-beta1 make px4_fmu-v5_default
ls -l PX4/build/px4_fmu-v5_default/px4_fmu-v5_default.elf
```

Athrill loads the **ELF** (it needs the symbols), not the `.px4` upload
file, and starts PX4 itself at `0x08008000`, after the place of the
bootloader, which is not run. `px4_sitl` is not a substitute: the point is
the board-target build. On Linux the container's files under `PX4/build`
belong to root (`sudo chown -R "$USER" PX4` if you want them back).

### 3. Run PX4 SIH

[examples/px4-sih](examples/px4-sih) has the memory map, the device
configuration and a script. Its first run makes the microSD card image
(1 GB FAT32) and selects the SIH airframe (`SYS_AUTOSTART 1100`, kept in the
emulated F-RAM, `fram.bin`); every run then boots PX4 with its console on
the terminal. On Linux the SD card image needs `mkfs.vfat`
(`sudo apt install dosfstools`).

```sh
cd athrill-target-stm32
examples/px4-sih/run.sh ../PX4/build/px4_fmu-v5_default/px4_fmu-v5_default.elf
```

PX4's TELEM1 is on TCP `127.0.0.1:5760`: add a TCP link to it in
**QGroundControl** (Application Settings, Comm Links), or use pymavlink with
`tcp:127.0.0.1:5760`, then arm and take off as usual. The vehicle starts at
PX4's SIH home (Zurich). Stop with Ctrl+C. The ULog files are in the SD
card image (`examples/px4-sih/sd.img`, `log/`), which macOS mounts with
`hdiutil attach -imagekey diskimage-class=CRawDiskImage sd.img`; on Linux
mtools reads it without mounting (`mdir -/ -i sd.img ::/log`,
`mcopy -i sd.img ::/log/sess000/log100.ulg .`). [docs/sih.md](docs/sih.md) explains each setting.

QGroundControl warns that HITL is enabled (`SYS_HITL=2` is how SIH runs)
and asks for radio and flight-mode setup (no RC): both are expected.

These steps were run as written on an **Apple Silicon Mac** (macOS) and on
**Windows 11 / WSL2 Ubuntu** (Intel Core Ultra 7 155H, Docker inside WSL2).
On WSL2: `sudo apt install dosfstools mtools`, and the Python packages in a
venv. QGroundControl on Windows connects to PX4 in WSL2 with a TCP link to
`127.0.0.1:5760` (WSL's localhost forwarding), shows the vehicle at Zurich
and flies it. Hovering runs at RTF 1.92 on the Mac (M2 Pro), 1.29 on that
PC; the machines and more numbers are in
[docs/verification.md](docs/verification.md#test-machines).

## Performance and validation

Results from **2026-11-04**, on an Apple Silicon Mac (M2 Pro), with
virtual STM32 clock at 216 MHz (both hosts in
[docs/verification.md](docs/verification.md#speed)):

| Workload | Real-time factor (RTF) |
| --- | ---: |
| PX4 SIH hover, QGroundControl/MAVLink and SD logging, after optimization | **1.92–1.94** (two runs) |

An RTF above 1 means virtual simulation time advances faster than elapsed
wall-clock time for the measured workload. These are *workload- and
host-specific measurements*, not a CPU clock-accuracy result or a comparative
benchmark against other emulators. See [performance notes](docs/performance.md).

Athrill's instruction decoders are generated from Arm machine-readable
architecture data. The Arm instruction execution path uses shared
[athrill-arm-common](https://github.com/hakoniwalab/athrill-arm-common)
pseudo-code. By default the most frequent instructions use fast executors,
and floating-point arithmetic uses the host FPU wherever IEEE 754 gives the
pseudo-code's exact bits (other cases fall back to the pseudo-code); both are
checked against the reference path bit for bit on millions of random cases,
and `DEVICE_CONFIG_ARM_EXEC_MODE strict` / `DEVICE_CONFIG_ARM_FP_MODE strict`
run the pseudo-code only. Instruction, FP, and exception tests are compared with QEMU on suitable
Arm targets. See [testing](docs/testing.md) and [decoder details](docs/decoder.md).

A virtual-clock-based event model and Hakoniwa lockstep integration are
central design choices. A flight driven only by virtual time repeats exactly
(see [Verification](#verification)); input timed by the wall clock, such as
QGroundControl's commands over TCP, arrives at run-dependent virtual times,
so **complete deterministic replay across all external inputs and all
configurations has not been established**. The emulation is neither
instruction-cycle accurate nor suitable as a substitute for board-level
electrical, safety, or flight validation.

## Verification

[examples/px4-sih/verify](examples/px4-sih/verify) checks a PX4 SIH flight
end to end with one command (about 2 minutes). From one starting state
(F-RAM and SD card images after the first boot), it

- flies take off, hover, land three times from the NSH console at fixed
  virtual times, twice in the default mode and once in strict mode, and
  requires the F-RAM, the whole SD card image and the console output (but
  the strict run's two mode lines) to be identical;
- checks that flight's ULog: EKF2 against SIH's ground truth, attitude
  tracking, altitude and position hold, the arm-to-disarm sequence;
- flies arm, take off, "go to" 5 m north, land over MAVLink as
  QGroundControl does, and checks its log the same way.

What it runs, every threshold and what it does not show are in
[docs/verification.md](docs/verification.md#what-verifysh-does).

```sh
examples/px4-sih/verify/verify.sh ../PX4/build/px4_fmu-v5_default/px4_fmu-v5_default.elf
```

It needs python3 with numpy, pyulog and pymavlink (matplotlib for the
charts: `pip install numpy pyulog pymavlink matplotlib`), and on Linux mtools
to read the SD card image; without pymavlink it skips the MAVLink flight and
reports `PARTIAL PASS`. Results on
2026-11-04, all PASS on the Mac and on WSL2; charts, hashes and both hosts'
numbers are in [docs/verification.md](docs/verification.md):

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/verification/altitude-dark.png">
  <img alt="Altitude over time: setpoint, SIH ground truth and EKF2 estimate overlap from takeoff through the 2.5 m hover to touchdown" src="docs/verification/altitude-light.png" width="720">
</picture>


| Check | Result |
| --- | --- |
| **Determinism**: take off, hover, land, typed into the NSH console at fixed virtual times, three runs from the same F-RAM and SD card images | Two runs in the default mode and one in strict mode (Arm pseudo-code only, no fast executors, no host FPU): identical F-RAM, **whole SD card image including the ULog**, and console output apart from the strict run's two mode lines; 3,507,861,181 instructions each |
| **EKF2** (estimate vs SIH ground truth, from the ULog) | no filter faults; innovation test ratios at most 0.26 (gate 1); position error RMS 4 cm horizontal, 2 cm vertical; tilt 0.12°, heading 1.8° |
| **Attitude and position control** | hover at 2.489 m mean (2.451–2.504 m) for a 2.5 m takeoff; horizontal drift at most 7 cm |
| **QGroundControl-style MAVLink**: arm, take off, "go to" 5 m north, hover, land | all commands acknowledged; up to 21° tilt with tilt tracking RMS 1.2°; holds the target within 18 cm; lands and disarms; ULog checks as above pass |

The checks and their limits are in
[analyze_ulog.py](examples/px4-sih/verify/analyze_ulog.py) (thresholds chosen
for this SIH vehicle, not PX4 requirements). Runs that receive input over
TCP are checked by their logs, not for identity.

## Beyond SIH: PX4's sensor drivers and external physics

A second, separate experiment exercises the parts SIH bypasses. PX4's own
drivers read the emulated ICM-20689, MS5611, IST8310 and u-blox chips, whose
samples come from **Hakoniwa Drone PRO** (the x500 in MuJoCo), and PX4's
PWM pulses on the FMU AUX outputs go back to it as motor commands, in
lockstep. The x500 takes off and hovers at about 2.5 m; the whole system
runs at RTF 1.48. Hakoniwa Drone PRO is a separate component that is **not
publicly available**; [docs/hakoniwa.md](docs/hakoniwa.md) records the setup
for those who have it. The SIH demonstration does not need it.

## Related work and motivation

This is not a claim that MCU emulation is new, or that other emulators cannot
run deterministically. For example, [QEMU supports instruction counting
and record/replay](https://www.qemu.org/docs/master/system/replay.html), and
[ArduPilot documents firmware emulation with
Renode](https://ardupilot.org/dev/docs/renode-emulation.html).

[docs/athrill.md](docs/athrill.md) explains what Athrill is and how it
differs from QEMU and Renode. In one sentence, against Renode, which already
models the STM32F7 for ArduPilot: Athrill advances in virtual-time lockstep
with external physics (Hakoniwa), generates its decoders from Arm's
machine-readable data, and has a pseudo-code-only strict mode that a whole
PX4 flight matches bit for bit. Our focus is narrower: a **lightweight,
inspectable PX4 FMUv5 firmware execution path** that can be connected to the Hakoniwa simulation environment,
with explicit virtual time and testable functional peripheral models. We do
not yet claim a measured speed advantage over QEMU or Renode.

An earlier [Athrill RISC-V target](https://github.com/hakoniwalab/athrill-target-riscv)
runs PX4 on the AIFoundry Erbium board, including an optional MAVLink
configuration for QGroundControl. Erbium is a board target in upstream PX4
([`boards/aifoundry/erbium`](https://github.com/PX4/PX4-Autopilot/tree/main/boards/aifoundry/erbium),
`aifoundry_erbium_default`). Athrill
([toppers/athrill](https://github.com/toppers/athrill), branch `cpp`)
supports V850, RH850, RISC-V and Armv7-M (this target); PX4 runs on two of
them, RISC-V (RV64) and Armv7-M ([docs/athrill.md](docs/athrill.md)). Starting with
the familiar **Pixhawk 4 / FMUv5** makes this work easier to evaluate for
developers using STM32-based PX4 boards.

Feedback from PX4 developers on useful test cases, missing hardware models,
reproduction steps, and limitations is very welcome.

## Design and license

- [Athrill, and how it differs from QEMU and Renode](docs/athrill.md)
- [Design decisions and staged goals](docs/design.md)
- [Survey of existing approaches](docs/survey.md)
- [Test strategy](docs/testing.md)
- [Performance measurements](docs/performance.md)

`src/cpu/decoder/t32_decode16.{h,c}` and `t32_decode32.{h,c}` are generated
from Arm's Architecture Machine Readable Specification and are provided under
Arm's BSD 3-clause licence, reproduced in those files (Copyright 2010–2026
Arm Limited or its affiliates). The rest of this repository is under the
[MIT licence](LICENSE).

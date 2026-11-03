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

## 6. Consequences for the design

- ArduPilot on STM32 already has an emulator path (Renode). Starting with PX4,
  as decided, is where this target adds something.
- Model three sensors at first (one IMU, MS5611, IST8310) and a GPS.
- The motor-output path (AUX outputs or a px4io model) is to be decided.
- The instruction decoder's source is to be decided after checking the terms
  of Arm's data and other generators.

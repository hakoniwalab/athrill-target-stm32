# PX4 on the emulated Pixhawk 4 with Hakoniwa Drone

The stock `px4_fmu-v5_default` firmware flies the Hakoniwa Drone x500 in MuJoCo:
- **Hakoniwa Drone** computes the vehicle and writes its sensor samples to Hakoniwa (`hil_sensor`, `hil_gps`).
- **The emulated chips** of the board (ICM-20689, MS5611, IST8310, u-blox M8) give them to PX4's own drivers.
- **The PWM pulses** of the FMU outputs go back to Hakoniwa Drone as motor commands (`hil_actuator`).

Both sides move in lockstep on the data: Athrill runs the CPU up to the time of each sensor sample.

What you need, all public:
- [Hakoniwa Drone](https://github.com/toppers/hakoniwa-drone-core) v5.0.0 or later: the free edition, or
  Hakoniwa Drone PRO. It provides the aircraft service, the airframe `config/drone/x500-athrill-px4` and the PDU
  definition `config/pdudef/drone-pdudef-athrill-px4.json`.
- The Hakoniwa sensor device, from [athrill-device](https://github.com/hakoniwalab/athrill-device)
  (`board/common`, built for this board as described in its
  [board/stm32f7xx](https://github.com/hakoniwalab/athrill-device/blob/main/board/stm32f7xx/README.md)).
- This repository's `athrill3` and the PX4 ELF.

## What the board adds

- **Motor commands**: the ESCs on the FMU outputs AUX1 to AUX8 (TIM1 CH4 to
  CH1, TIM4 CH2, CH3, TIM12 CH1, CH2, as PX4's `timer_config.cpp`). A pulse
  of 1000 us is 0, 2000 us is 1 (`PWM_AUX_MIN/MAX` defaults); the commands
  go to the sensor device whenever one changes, and the device writes the
  latest ones with each sensor sample. Without the PX4IO co-processor (not
  modelled) the motors are on AUX1 to AUX4 (`PWM_AUX_FUNC1..4`).
- **MS5611 noise**: the ADC noise of the datasheet (RMS 0.012 mbar at
  OSR 4096). Hakoniwa Drone's barometer noise is below the chip's resolution, so
  without it the readings at rest never change and PX4 takes the barometer
  as stale ("BARO #0 failed: STALE!", arming denied).

## Files

- Memory (`memory.txt`): ROM 0x08000000 2048 KB, RAM 0x20000000 512 KB, and
  `DEV, 0x60000000, <libhako_sensor_device.dylib>` (`.so` on Linux), the sensor
  device of athrill-device built with `ATHRILL_ADDRESS_WIDTH=32` against the same
  Hakoniwa Foundation as Hakoniwa Drone.
- Device configuration:

```
DEVICE_CPU_FREQ 216
DEBUG_FUNC_ENABLE_SKIP_CLOCK 1
DEVICE_CONFIG_STM32_BOOT_ADDR 0x08008000
DISABLE_SYMBOL_UNDERSCORE 1
DEVICE_CONFIG_STM32_CONSOLE_UART 7
DEVICE_CONFIG_STM32_BOARD fmu-v5
DEVICE_CONFIG_STM32_FRAM_FILE fram_x500.bin
DEVICE_CONFIG_STM32_SDCARD_IMAGE sd_x500.img
DEVICE_CONFIG_STM32_UART2_TCP_PORT 5760
HAKO_SENSOR_DEVICE_ASSET athrill
HAKO_SENSOR_DEVICE_ROBOT Drone
HAKO_SENSOR_DEVICE_SENSOR_CHANNEL 19
HAKO_SENSOR_DEVICE_GPS_CHANNEL 20
HAKO_SENSOR_DEVICE_ACTUATOR_CHANNEL 21
HAKO_SENSOR_DEVICE_IRQ 1000
```

`HAKO_SENSOR_DEVICE_IRQ 1000` keeps the device's data-ready interrupt (a
RISC-V PLIC source) away from the STM32's NVIC: the chips have their own.
The F-RAM and SD card files are their own, not the SIH ones (the SD card
keeps a parameter backup that PX4 loads when the F-RAM is empty).

## First boot: the parameters

Copy `docs/px4/x500-hakoniwa-params.sh` into the SD card image (mount it
with `hdiutil attach -imagekey diskimage-class=CRawDiskImage sd_x500.img`),
then give NSH, with `HAKO_SENSOR_DEVICE_TEST_PERIOD 4000` in place of the
`HAKO_SENSOR_DEVICE_*` channel lines (no Hakoniwa needed):

```
sleep 6
sh /fs/microsd/x500-hakoniwa-params.sh
```

The parameters land in the F-RAM file. Run them as a script: pasted into
the console, more than its 600-byte receive buffer arrives while NSH is
still busy with the first commands, and the rest is lost (no flow control
on the console, as on the board).

The calibration IDs in the script are those of the emulated chips
(`listener sensor_accel` etc.): ICM-20689 on SPI1 3932170, IST8310 on I2C3
396825.

## Run

In this order (as for the RISC-V target): Hakoniwa Drone's aircraft service, the only conductor
(`hako_aircraft_service_px4 127.0.0.1 4560 config/drone/x500-athrill-px4
config/pdudef/drone-pdudef-athrill-px4.json`), Athrill (`athrill3 -c1 -m
memory.txt -d device_config.txt -t -1 px4_fmu-v5_default.elf`), `hako-cmd
start`. Run all three without the old Hakoniwa Core on `DYLD_LIBRARY_PATH`;
Athrill with the Foundation's `lib` there. QGroundControl or pymavlink on
`tcp:127.0.0.1:5760`.

**Take-off over MAVLink:** the altitude of `MAV_CMD_NAV_TAKEOFF` is above mean
sea level. Give the home altitude plus the height to climb: 2.5 alone makes PX4
answer "Already higher than takeoff altitude" and stay on the ground.
QGroundControl does this for you.

**At rest, "MAG #0 failed: STALE!"** can appear: the magnetometer noise of the
airframe is below the IST8310's resolution, so the readings of a vehicle at rest
do not change. It does not stop the arming or the take-off.

## Results

2026-11-05, Hakoniwa Drone v5.0.0 (sensor model v2), the device from
athrill-device: arm, then take-off to 2.51 m (target 2.5 m).

2026-11-04, Hakoniwa Drone PRO (Apple M-series host, no viewer):

- Arm and `MAV_CMD_NAV_TAKEOFF` over MAVLink, and from QGroundControl: the
  x500 climbs and hovers at 2.50 to 2.51 m of relative altitude (takeoff
  target 2.5 m). The very first flight, on a parameter set just written,
  held 2.31 to 2.33 m; later flights with the same files held 2.5 m. That
  first run was not looked into further.
- RTF 1.48 for the whole system while hovering (Athrill 91 % of a core,
  Hakoniwa Drone 111 %); PX4 SIH alone runs at 1.94 (`docs/sih.md`): here the
  chips' drivers run (IMU data ready at 8 kHz with SPI DMA) and the lockstep
  waits for Hakoniwa Drone's physics.

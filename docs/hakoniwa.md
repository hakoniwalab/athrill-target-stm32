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
- [athrill-device](https://github.com/hakoniwalab/athrill-device): the board's Devicetree
  (`boards/pixhawk4/pixhawk4-hakoniwa.dtb`) and the Hakoniwa sensor device (`board/common`), which Athrill loads
  as the board's plugin.
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

## Run it

The commands are for macOS on Apple silicon. On Linux x86_64, see [On Linux](#on-linux) for the few differences.

You need git, CMake, a C/C++ compiler, python3 with `pymavlink` (for the take-off check), and GLFW for Hakoniwa
Drone (`brew install glfw`). Everything goes into one workspace:

```bash
mkdir -p "$HOME/athrill-hakoniwa"
cd "$HOME/athrill-hakoniwa"
git clone --branch v0.3.0 https://github.com/hakoniwalab/athrill-target-stm32.git
```

### 1. Hakoniwa core

Hakoniwa Drone's binaries carry the core's libraries, but the sensor device is built against the core, and
`hako-cmd` starts the simulation. Install the same revision of
[hakoniwa-core-pro](https://github.com/hakoniwalab/hakoniwa-core-pro) as Hakoniwa Drone v5.0.0 was built with:

```bash
cd "$HOME/athrill-hakoniwa"
git clone https://github.com/hakoniwalab/hakoniwa-core-pro.git
git -C hakoniwa-core-pro checkout "$(cat athrill-target-stm32/examples/px4-hakoniwa/hakoniwa-core-pro-revision.txt)"
git -C hakoniwa-core-pro submodule update --init --recursive
(cd hakoniwa-core-pro && ./install.bash)
```

`install.bash` builds the core and installs it in `/usr/local/hakoniwa`, with its configuration in `/etc/hakoniwa`
(it asks for your password for `sudo`).

### 2. Hakoniwa Drone

The free edition, [hakoniwa-drone-core](https://github.com/toppers/hakoniwa-drone-core) v5.0.0: its repository
for the airframe and the PDU definition, and the binaries of its release.

```bash
cd "$HOME/athrill-hakoniwa"
git clone --branch v5.0.0 https://github.com/toppers/hakoniwa-drone-core.git
cd hakoniwa-drone-core
curl -LO https://github.com/toppers/hakoniwa-drone-core/releases/download/v5.0.0/mac.zip
unzip -q mac.zip
```

### 3. The sensor device

The Hakoniwa sensor device of [athrill-device](https://github.com/hakoniwalab/athrill-device), built for this
board (32-bit), against the core of step 1, with the PDU types of the same core. `run.sh` takes the board's
Devicetree from this athrill-device checkout, next to `athrill-target-stm32`:

```bash
cd "$HOME/athrill-hakoniwa"
git clone https://github.com/toppers/athrill.git
git -C athrill checkout bc8d76a
git clone https://github.com/hakoniwalab/athrill-device.git
git -C athrill-device checkout ff23daf
cmake -S athrill-device/board/common -B athrill-device/build-stm32 -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/usr/local/hakoniwa -DATHRILL_ADDRESS_WIDTH=32 \
  -DHAKO_PDU_REGISTRY_DIR="$PWD/hakoniwa-core-pro/hakoniwa-pdu-registry"
cmake --build athrill-device/build-stm32 -j
```

The result is `athrill-device/build-stm32/libhako_sensor_device.dylib`.

### 4. athrill3 and PX4

From this repository's release v0.3.0:

```bash
cd "$HOME/athrill-hakoniwa"
curl -LO https://github.com/hakoniwalab/athrill-target-stm32/releases/download/v0.3.0/athrill3-v0.3.0-macos-arm64.tar.gz
curl -LO https://github.com/hakoniwalab/athrill-target-stm32/releases/download/v0.3.0/px4_fmu-v5_default.elf
curl -LO https://github.com/hakoniwalab/athrill-target-stm32/releases/download/v0.3.0/SHA256SUMS
shasum -a 256 -c --ignore-missing SHA256SUMS
tar xzf athrill3-v0.3.0-macos-arm64.tar.gz
```

### 5. Fly

If a Hakoniwa core of another version ran on this machine before, its runtime files are still in
`/var/lib/hakoniwa/mmap`. Clear them once, with no Hakoniwa process running:

```bash
/usr/local/hakoniwa/bin/hako-cmd recover
```

Otherwise the aircraft service stops at start with `HAKONIWA_FAIL_STOP ... (legacy flock.bin)`.


Three terminals. In the first, Hakoniwa Drone's aircraft service: the only conductor, with the MuJoCo viewer
at real time.

```bash
cd "$HOME/athrill-hakoniwa/hakoniwa-drone-core"
mac/mac-main_hako_aircraft_service_px4 127.0.0.1 4560 \
  config/drone/x500-athrill-px4 config/pdudef/drone-pdudef-athrill-px4.json --mujoco-viewer --real-sleep-msec 1
```

In the second, PX4 on Athrill:

```bash
cd "$HOME/athrill-hakoniwa"
DYLD_LIBRARY_PATH=/usr/local/hakoniwa/lib athrill-target-stm32/examples/px4-hakoniwa/run.sh \
  px4_fmu-v5_default.elf athrill3-v0.3.0-macos-arm64/athrill3 athrill-device/build-stm32/libhako_sensor_device.dylib
```

The first run makes the emulated SD card with the x500 parameters (`docs/px4/x500-hakoniwa-params.sh`) and runs
them once, without Hakoniwa: a few minutes, then the parameters are in the emulated F-RAM (`fram.bin`). Wait for
`PX4 on Athrill with Hakoniwa`. Later runs start at once. To start again from scratch, delete `fram.bin` and
`sd.img` in `examples/px4-hakoniwa`.

In the third, start the simulation, then take off to 2.5 m and check the hover:

```bash
cd "$HOME/athrill-hakoniwa"
/usr/local/hakoniwa/bin/hako-cmd start
python3 athrill-target-stm32/examples/px4-hakoniwa/fly.py
```

`fly.py` prints `PASS` when the x500 holds 2.5 m (within 0.3 m) for the last 10 s of a 40 s hover. You can also
connect QGroundControl to TCP `127.0.0.1:5760` instead, and arm and take off from there.

To stop, press Ctrl+C in the first two terminals, then run `/usr/local/hakoniwa/bin/hako-cmd recover`.

### On Linux

- Hakoniwa Drone: `lnx.zip` in place of `mac.zip`, and `lnx/linux-main_hako_aircraft_service_px4`. `lnx.zip`
  does not carry the core's libraries: put `/usr/local/hakoniwa/lib` on `LD_LIBRARY_PATH` for it too. It needs
  `libglfw3` and `libopengl0`.
- The sensor device is `libhako_sensor_device.so`.
- `athrill3-v0.3.0-linux-x86_64.tar.gz`, checked with `sha256sum -c --ignore-missing SHA256SUMS`.
- `LD_LIBRARY_PATH` in place of `DYLD_LIBRARY_PATH` for `run.sh`.

## ArduPilot

The official ArduCopter firmware for the Pixhawk 4 (`Pixhawk4`, V4.7.2, from firmware.ardupilot.org) flies the same
x500 on the same board and the same sensor device. ArduPilot reads the emulated chips with its own drivers. Do
steps 1 to 4 above first, in the same workspace.

### A1. The firmware

```bash
cd "$HOME/athrill-hakoniwa"
curl -LO https://firmware.ardupilot.org/Copter/stable-4.7.2/Pixhawk4/arducopter.elf
shasum -a 256 arducopter.elf
```

The SHA-256 of V4.7.2 is `cfa1b3f04f326ab91760c53b93503b669d4d62e2d43dd1ffc4d09cb15198eb2c`. ArduPilot is GPL-3.0, so
this project does not redistribute it. `stable-4.7.2` keeps this version; `stable` moves with each release, and with
another version the parameter names may differ (4.7 renamed many of 4.6's).

### A2. Fly

In the first terminal, Hakoniwa Drone's aircraft service with the ArduPilot airframe of the example. It is the
x500 of `x500-athrill-px4` without the 4 ms soft delay of the IMU (PX4 SITL's) and with the GPS accuracy ArduPilot
SITL reports.

```bash
cd "$HOME/athrill-hakoniwa/hakoniwa-drone-core"
mac/mac-main_hako_aircraft_service_px4 127.0.0.1 4560 \
  "$HOME/athrill-hakoniwa/athrill-target-stm32/examples/ardupilot-hakoniwa/x500-athrill-ardupilot" \
  config/pdudef/drone-pdudef-athrill-px4.json --mujoco-viewer --real-sleep-msec 1
```

In the second, ArduCopter on Athrill:

```bash
cd "$HOME/athrill-hakoniwa"
DYLD_LIBRARY_PATH=/usr/local/hakoniwa/lib athrill-target-stm32/examples/ardupilot-hakoniwa/run.sh \
  arducopter.elf athrill3-v0.3.0-macos-arm64/athrill3 athrill-device/build-stm32/libhako_sensor_device.dylib
```

The first run boots ArduCopter on the test vehicle and writes the x500 parameters over MAVLink
(`load-params.py`): it prints `set ... of ...` and keeps them in `fram.bin`. Wait for
`ArduCopter on Athrill with Hakoniwa`.

In the third, start the simulation, then take off to 2.5 m and check the hover:

```bash
cd "$HOME/athrill-hakoniwa"
/usr/local/hakoniwa/bin/hako-cmd start
python3 athrill-target-stm32/examples/ardupilot-hakoniwa/fly.py
```

`fly.py` waits for the EKF (about 100 s with the viewer, once the GPS is used), sets GUIDED, arms, takes off and
prints `PASS` when the x500 holds 2.5 m (within 0.3 m) for the last 10 s of a 40 s hover, counted in the vehicle's
time. Mission Planner or QGroundControl can connect to TCP `127.0.0.1:5760` instead.

What the example sets (`x500-hakoniwa-ardupilot.parm`):
- the x500's PID tuning result for ArduPilot from Hakoniwa Drone PRO (sensor model v2), as flown in ArduPilot SITL
  4.6.3, converted to 4.7 names and units;
- `BRD_IO_ENABLE 0` (no IO co-processor: the FMU outputs are SERVO1 to 8, the motors on SERVO1 to 4 = AUX1 to 4);
- `COMPASS_DEC` for the x500's place with `COMPASS_AUTODEC 0`;
- the checks as in ArduPilot SITL (`ARMING_SKIPCHK -1`, the 4.7 form of SITL's `ARMING_CHECK 0`), no safety switch
  (`BRD_SAFETY_DEFLT 0`) and no RC failsafe (`FS_THR_ENABLE 0`), as the simulation has no RC and no switch.

The board configuration (`device_config.txt`) lets a branch to itself wait like `WFI` (`DEVICE_CONFIG_SPIN_HALT`,
[ardupilot.md](ardupilot.md)): ArduCopter's idle loop is `b .`, and the virtual clock then skips it.

## How it is set up

- **Memory**: ROM 0x08000000 2048 KB, RAM 0x20000000 512 KB (`memory.txt`).
- **Board**: athrill-device's `boards/pixhawk4/pixhawk4-hakoniwa.dtb`. It is the Pixhawk 4 with the sensor device
  as the source of the chips' samples and the sink of the PWM outputs. `run.sh` adds `DEVICE_CONFIG_DEVICE_TREE`
  and the sensor device you give (`DEVICE_CONFIG_DEVICE_PLUGIN_0`) to the configuration of each run.
- **Device configuration**: [examples/px4-hakoniwa/device_config.txt](../examples/px4-hakoniwa/device_config.txt).
  `HAKO_SENSOR_DEVICE_IRQ 1000` keeps the device's own data-ready interrupt away from the STM32's NVIC: the
  chips raise their own. The F-RAM and SD card files are their own, not the SIH ones (the SD card keeps a
  parameter backup that PX4 loads when the F-RAM is empty).
- **First boot**: [first-boot-config.txt](../examples/px4-hakoniwa/first-boot-config.txt) gives a vehicle at
  rest every 4 ms (`HAKO_SENSOR_DEVICE_TEST_PERIOD 4000`), without Hakoniwa, and NSH runs the parameter script
  from the SD card. The test vehicle (`HAKO_SENSOR_DEVICE_TEST_*`) is where the Hakoniwa Drone x500 starts:
  the position of its airframe (`simulation.location`, plus the 0.21 m of the vehicle's origin above the
  ground), and the pressure, temperature and magnetic field Hakoniwa Drone gives there. PX4 stores a barometer
  offset and the magnetic declination at this boot. With another place, the first flight in Hakoniwa held
  2.72 m instead of 2.5 m. For another airframe or place, change these lines. It runs as a script: pasted into the console, more than its 600-byte receive buffer arrives
  while NSH is still busy with the first commands, and the rest is lost (no flow control on the console, as on
  the board). `put-sd-file.py` writes the script into the blank SD card image, the same byte for byte on every
  host.
- **Parameters**: the airframe x500 v2 with the parameters of Hakoniwa Drone's x500, the motors on AUX1 to AUX4,
  and the calibration IDs of the emulated chips (`listener sensor_accel` etc.): ICM-20689 on SPI1 3932170,
  IST8310 on I2C3 396825.

**Take-off over MAVLink:** the altitude of `MAV_CMD_NAV_TAKEOFF` is above mean
sea level. Give the home altitude plus the height to climb: 2.5 alone makes PX4
answer "Already higher than takeoff altitude" and stay on the ground.
QGroundControl does this for you.

**At rest, "MAG #0 failed: STALE!"** can appear: the magnetometer noise of the
airframe is below the IST8310's resolution, so the readings of a vehicle at rest
do not change. It does not stop the arming or the take-off.

## Results

2026-11-08, after the boards moved to their Devicetree (`run.sh` loads `pixhawk4-hakoniwa.dtb` and the sensor
device as its plugin), macOS (Apple M2 Pro), Hakoniwa Drone v5.0.0, athrill3 from main with athrill-device main,
no viewer, from no `fram.bin` and `sd.img`:
- **PX4** (the ELF of release v0.1.1): first boot 18 s, then `fly.py` 3/3 PASS, 2.49 to 2.52 m in the last 10 s,
  RTF 1.61 to 1.65.
- **ArduPilot** (V4.7.2): the parameter write 63 s, then `fly.py` 3/3 PASS, 2.48 to 2.52 m in the last 10 s,
  RTF 0.99 to 1.09.

2026-11-05, **ArduPilot** (the steps of [ArduPilot](#ardupilot)), macOS (Apple M2 Pro), released Hakoniwa Drone
v5.0.0 `mac.zip`, athrill3 from main, the sensor device from athrill-device main: from no `fram.bin` and `sd.img`,
the parameter write took 96 s (97 of 97 set and read back). Then `fly.py` passed three times in a row with the MuJoCo
viewer at real time: EKF ready, GUIDED take-off, 2.48 to 2.52 m in the last 10 s. Without the viewer: PASS, **RTF
1.15**.

2026-11-05, after the sensor device reports Athrill's own time to Hakoniwa (athrill-device #3: before, it reported
the HIL time, so the lockstep was paced by the data exchange only), macOS (Apple M2 Pro), released Hakoniwa Drone
v5.0.0 `mac.zip` and this release's `athrill3` and ELF: `fly.py` 3/3 PASS, 2.48 to 2.50 m with the MuJoCo viewer at
real time, the first boot (15 s) included. Without the viewer the whole system runs at **RTF 1.85 to 1.86** while
hovering (`fly.py` prints it, in the vehicle's time over the wall time of the hold).

2026-11-05, macOS (Apple M2 Pro), the steps above with:
- athrill3 built from this branch with Athrill v3.0.0, the PX4 ELF of release v0.1.1 (the same PX4 v1.17.0 build);
- the sensor device built as in step 3, against the core of step 1 (`ed26571`);
- in place of `mac.zip` (Hakoniwa Drone v5.0.0 was not released yet), Hakoniwa Drone PRO built from the same
  v5.0.0 code, run with the libraries of that core.

Starting from no `fram.bin` and `sd.img`, the first boot took 17 s. Then `fly.py` passed three times in a row,
with the MuJoCo viewer at real time: 2.48 to 2.50 m in the last 10 s, the first flight included. The parameters
read over MAVLink at the 2nd and 3rd boots were identical. Ctrl+C stops Athrill and the aircraft service.

2026-11-05, Linux (WSL2, Ubuntu 24.04), the steps above with the released Hakoniwa Drone v5.0.0 `lnx.zip` and this
release's `athrill3` and ELF: first boot 52 s; `fly.py` 3/3 PASS, 2.35 to 2.48 m in the last 10 s, MuJoCo viewer at
real time. Ctrl+C stops Athrill and the aircraft service.

2026-11-04, Hakoniwa Drone PRO (no viewer): RTF 1.48 for the whole system while hovering (Athrill 91 % of a
core, Hakoniwa Drone 111 %); PX4 SIH alone runs at 1.94 (`docs/sih.md`): here the chips' drivers run (IMU data
ready at 8 kHz with SPI DMA) and the lockstep waits for Hakoniwa Drone's physics. The first flight on a
parameter set just written then held 2.31 m: the test vehicle of the first boot was elsewhere (fixed in
athrill-device since, and given by `first-boot-config.txt`).

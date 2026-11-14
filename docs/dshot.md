# DShot ESCs on the emulated Pixhawk 4

The stock PX4 and ArduCopter firmware drive the x500's motors with **DShot600** instead of PWM, through emulated
ESCs. A stopped motor can be injected, and PX4 detects it from the ESC telemetry:

```text
PX4 / ArduCopter                         Athrill                                  Hakoniwa Drone
  DShot driver ── TIM1 DMA burst ──▶ STM32 timer ──▶ DShot ESC (x4) ──▶ motor command ──▶ x500 in MuJoCo
  ESC telemetry ◀── UART4 (KISS) ◀─────────────────── telemetry ◀────── motor status (Drone PRO)
                                                        ▲
                                                 fault model: motor stop at a time, or from a browser
```

- **The DShot frames are the firmware's own.** PX4's and ArduPilot's drivers fill a DMA buffer with one
  compare value per bit (a short high for 0, a long one for 1) and let the timer's DMA burst write them to
  TIM1's four compare registers, one bit period after another. The emulated timer does that burst; each ESC reads
  its channel's high times, and checks the bit rate (150/300/600/1200 kbit/s, within 15 %), the duty of each bit, the
  frame's CRC and the idle gap between frames. A frame that fails a check is dropped, as a real ESC does; after
  100 ms without a valid frame the ESC stops its motor.
- **The throttle goes to the aircraft.** A DShot value of 48 to 2047 is a throttle of 0 to 1. Hakoniwa Drone gets
  it as the motor command, as with PWM.
- **The ESCs answer on a serial port.** When the firmware sets the telemetry bit, the ESC sends a 10-byte KISS
  telemetry record (temperature, voltage, current, consumption, eRPM, CRC-8) on UART4. With the public Hakoniwa
  Drone the values come from a simple model of the throttle (eRPM 70000·t, current 30 A·t², 16 V). With the
  experimental build of Hakoniwa Drone PRO, which reports the state of each rotor, they are the aircraft's: the rotor
  speed, the current, the battery voltage.
- **A fault model sits between the firmware and the ESCs.** It can drop or replace a command or a telemetry
  record. The one shipped stops a motor at a given time (`DEVICE_CONFIG_ESC_MOTOR_STOP motor@time-ms`, motor 0 to 3,
  Athrill's virtual time in milliseconds since power-on). Several stops are joined with `+`:
  `0@30000+2@40000`.

PX4 v1.17 reports **Motor failure detected** when a running motor draws less current than its throttle asks
(below 2 A per unit of throttle above 20 %) for 100 ms. ArduCopter has no such check: the vehicle comes down
without a message.

The board is athrill-device's `boards/pixhawk4/pixhawk4-hakoniwa-dshot.dtb`: the Hakoniwa board of
[hakoniwa.md](hakoniwa.md), with the four ESCs on TIM1 (motors 1 to 4) and their telemetry on UART4 (TELEM4) in place
of the PWM outputs. The examples are [examples/px4-hakoniwa-dshot](../examples/px4-hakoniwa-dshot) and
[examples/ardupilot-hakoniwa-dshot](../examples/ardupilot-hakoniwa-dshot). They use the configuration files of
`px4-hakoniwa` and `ardupilot-hakoniwa` and add the DShot board and parameters.

## Build

Do steps 1 to 3 of [hakoniwa.md](hakoniwa.md#run-it) first (the Hakoniwa core, Hakoniwa Drone v5.0.0, the
sensor device), in the same workspace. The DShot ESCs are not in release v0.3.0's `athrill3`: build `athrill3` from
this repository's main with athrill-device `73a0f4d`, and the sensor device again from that revision.

```bash
cd "$HOME/athrill-hakoniwa"
git -C athrill-device fetch
git -C athrill-device checkout 73a0f4d
cmake --build athrill-device/build-stm32 -j
git clone https://github.com/hakoniwalab/athrill-arm-common.git
git -C athrill-arm-common checkout 60d8098
git -C athrill-target-stm32 fetch
git -C athrill-target-stm32 checkout main
cmake -S athrill-target-stm32 -B athrill-target-stm32/build -DCMAKE_BUILD_TYPE=Release
cmake --build athrill-target-stm32/build --parallel
```

`athrill`, `athrill-arm-common` and `athrill-device` are next to `athrill-target-stm32`, where its build finds them.
The result is `athrill-target-stm32/build/athrill3`. The PX4 ELF is the one of release v0.3.0 (step 4 of
[hakoniwa.md](hakoniwa.md#4-athrill3-and-px4)), stock PX4 v1.17.0 `px4_fmu-v5_default`.

## PX4

### Fly

The same three terminals as in [hakoniwa.md](hakoniwa.md#5-fly). In the first, Hakoniwa Drone's aircraft service:

```bash
cd "$HOME/athrill-hakoniwa/hakoniwa-drone-core"
mac/mac-main_hako_aircraft_service_px4 127.0.0.1 4560 \
  config/drone/x500-athrill-px4 config/pdudef/drone-pdudef-athrill-px4.json --mujoco-viewer --real-sleep-msec 1
```

In the second, PX4 on Athrill with the DShot board:

```bash
cd "$HOME/athrill-hakoniwa"
DYLD_LIBRARY_PATH=/usr/local/hakoniwa/lib athrill-target-stm32/examples/px4-hakoniwa-dshot/run.sh \
  px4_fmu-v5_default.elf athrill-target-stm32/build/athrill3 athrill-device/build-stm32/libhako_sensor_device.dylib
```

The first run writes the x500 parameters and the DShot ones (`dshot-params.sh`: `PWM_AUX_TIM0 -3` for DShot600 on
TIM1, `DSHOT_TEL_CFG 104` for the telemetry on TELEM4) into this example's own `fram.bin`, without Hakoniwa. Wait for
`PX4 on Athrill with DShot and Hakoniwa`.

In the third:

```bash
cd "$HOME/athrill-hakoniwa"
/usr/local/hakoniwa/bin/hako-cmd start
python3 athrill-target-stm32/examples/px4-hakoniwa/fly.py
```

`fly.py` takes off and prints `PASS` when the x500 holds 2.5 m, as with PWM. PX4's `esc_status` (in the ULog,
or `listener esc_status` on the console) shows the four ESCs online with their telemetry.

To stop, press Ctrl+C in the first two terminals, then run `/usr/local/hakoniwa/bin/hako-cmd recover`.

### Stop a motor

Give `run.sh` a file with the stop as a fourth argument. Motor 3 (index 2) at 40 s:

```bash
cd "$HOME/athrill-hakoniwa"
/usr/local/hakoniwa/bin/hako-cmd recover
printf 'DEVICE_CONFIG_ESC_MOTOR_STOP 2@40000\n' > motor3-stop.txt
```

Start the aircraft service again in the first terminal (the command above), then PX4 in the second:

```bash
cd "$HOME/athrill-hakoniwa"
DYLD_LIBRARY_PATH=/usr/local/hakoniwa/lib athrill-target-stm32/examples/px4-hakoniwa-dshot/run.sh \
  px4_fmu-v5_default.elf athrill-target-stm32/build/athrill3 athrill-device/build-stm32/libhako_sensor_device.dylib \
  motor3-stop.txt
```

In the third, once it prints `PX4 on Athrill with DShot and Hakoniwa`:

```bash
cd "$HOME/athrill-hakoniwa"
/usr/local/hakoniwa/bin/hako-cmd start
python3 athrill-target-stm32/examples/px4-hakoniwa-dshot/fly-motor-fault.py
```

The x500 takes off and hovers; at 40 s the ESC of motor 3 stops its motor and answers with no current. PX4 reports
`Motor failure detected`, the x500 rolls over and falls. `fly-motor-fault.py` prints `PASS` when the vehicle has
climbed, PX4 has reported the failure, and the vehicle is back on the ground. The ULog has `failure_detector_status`
with `motor_failure_mask` 4 (motor 3).

To record every frame the ESCs accept, add `DEVICE_CONFIG_DSHOT_ESC_FILE` and a file path to the same file: one line
per frame (time, ESC, value, telemetry bit), and one per telemetry record.

## Hakoniwa Drone PRO (experimental build)

The rest was verified with an experimental build of Hakoniwa Drone PRO that reports the state of each rotor to
the sensor device (rotor speed, current, voltage). It is not in the public release.

### In process, with the aircraft's motor status

The aircraft runs inside Athrill, as in [In process](hakoniwa.md#in-process), and the ESC telemetry is the
aircraft's. Compared with the steps above:
- the sensor device is athrill-device's in-process build with the motor status, `libhako_sensor_device_inproc_motor`
  (`-DHAKO_SENSOR_DEVICE_BUILD_INPROC=ON -DHAKO_SENSOR_DEVICE_BUILD_INPROC_MOTOR=ON`, built against Drone PRO);
- `HAKO_DRONE_ROOT` is the Drone PRO directory and `HAKO_AIRCRAFT_CONFIG_PATH` its `config/drone/x500-athrill-px4`
  (for ArduCopter, `examples/ardupilot-hakoniwa/x500-athrill-ardupilot`); `run.sh` then runs the aircraft in
  process: no aircraft service, no `hako-cmd`;
- the flight scripts are the same.

In the ULog's `esc_status`, the stopped motor's rpm runs down over about 0.15 s, the aircraft's rotor coming to
rest, and its current drops to 0.

### Athrill as the Hakoniwa asset: watch and stop a motor from a browser

Athrill can itself be the Hakoniwa asset, with the aircraft in process. The sensor device then writes the x500's
position, attitude and rotor speeds to Hakoniwa (`hako_msgs/DroneVisualStateArray`, robot
`DroneVisualStatePublisher`), and reads the rotor scales a browser sends (`hako_msgs/Disturbance`, robot `Drone`).
A rotor scale of 0 is a stopped motor: the sensor device is also the ESCs' fault model. Compared with the in-process
run:
- `athrill3` is built with `-DATHRILL_ENABLE_HAKONIWA=ON -DCMAKE_PREFIX_PATH=/usr/local/hakoniwa`;
- `run.sh` gets `DSHOT_BOARD=pixhawk4-hakoniwa-dshot-asset` (the same ESCs, and the sensor device as the fault
  model), `ATHRILL_OPTIONS="--hakoniwa athrill --pdudef PDUDEF --real-sleep-msec 10"` (Athrill is the asset
  `athrill` and the conductor, paced to real time), and the line `HAKO_SENSOR_DEVICE_PUBLISH_ASSET athrill` in its
  extra configuration. PDUDEF is `config/web_bridge_disturb/pdu/drone-disturb-visual-state.json` of
  [hakoniwa-pdu-bridge-core](https://github.com/hakoniwalab/hakoniwa-pdu-bridge-core) (`e071902`);
- the bridge's `hakoniwa-pdu-web-bridge`, built against the installed core, runs with
  `--config-root config/web_bridge_disturb --node-name web_bridge_disturb_node1 --delta-time-step-usec 20000
  --enable-ondemand`;
- `hako-cmd start`, then [hakoniwa-threejs-drone](https://github.com/hakoniwalab/hakoniwa-threejs-drone)
  (`eaf782d`) served over HTTP from its checkout, opened at
  `index.html?viewerConfigPath=/config/viewer-config-disturb.json`, **connect**; then
  `examples/px4-hakoniwa/fly.py`.

The x500 flies in the browser at real time. Set a rotor's slider to 0 in the fault-injection panel: the ESC stops
that motor, PX4 reports `Motor failure detected` and the x500 falls in the browser.

## ArduPilot

The ArduCopter V4.7.2 of [hakoniwa.md](hakoniwa.md#ardupilot) on the same DShot board. Get the firmware as in
[A1](hakoniwa.md#a1-the-firmware). In the first terminal, the aircraft service with the ArduPilot airframe:

```bash
cd "$HOME/athrill-hakoniwa/hakoniwa-drone-core"
mac/mac-main_hako_aircraft_service_px4 127.0.0.1 4560 \
  "$HOME/athrill-hakoniwa/athrill-target-stm32/examples/ardupilot-hakoniwa/x500-athrill-ardupilot" \
  config/pdudef/drone-pdudef-athrill-px4.json --mujoco-viewer --real-sleep-msec 1
```

In the second, ArduCopter, with a stop of motor 3 at 70 s (ArduCopter waits longer for its EKF than PX4):

```bash
cd "$HOME/athrill-hakoniwa"
printf 'DEVICE_CONFIG_ESC_MOTOR_STOP 2@70000\n' > motor3-stop-ap.txt
DYLD_LIBRARY_PATH=/usr/local/hakoniwa/lib athrill-target-stm32/examples/ardupilot-hakoniwa-dshot/run.sh \
  arducopter.elf athrill-target-stm32/build/athrill3 athrill-device/build-stm32/libhako_sensor_device.dylib \
  motor3-stop-ap.txt
```

The first run writes the x500 parameters and `dshot.parm` (`MOT_PWM_TYPE 6` for DShot600, `SERIAL4_PROTOCOL 16` for
the ESC telemetry on UART4, `SERVO_BLH_AUTO 1`, `SERVO_BLH_POLES 14`) over MAVLink, as
[A2](hakoniwa.md#a2-fly) does. Wait for `ArduCopter on Athrill with DShot and Hakoniwa`. In the third:

```bash
cd "$HOME/athrill-hakoniwa"
/usr/local/hakoniwa/bin/hako-cmd start
python3 athrill-target-stm32/examples/ardupilot-hakoniwa-dshot/fly-motor-fault.py
```

It takes off in GUIDED and prints `PASS` when the x500 is back on the ground after the stop. ArduCopter sends DShot
frames at 1 kHz and polls each ESC's telemetry in turn. For a flight without the stop, leave out the fourth argument
and use `examples/ardupilot-hakoniwa/fly.py`.

## Pixhawk 6C

PX4 v1.17.0 `px4_fmu-v6c_default` on the emulated Pixhawk 6C (STM32H743), with the same ESCs: motors 1 to 4 on TIM1
CH1 to CH4, whose update DMA request (DMAMUX1 request 15) carries PX4's burst, and the telemetry on USART2 (TELEM3).
The example is [examples/px4-v6c-hakoniwa-dshot](../examples/px4-v6c-hakoniwa-dshot). MAVLink is on TCP
`127.0.0.1:5760` (TELEM1).

The 6C needs athrill-device `6b6f588` or later: its DShot board, the transmit DMA of its MAVLink port (a one-byte
transfer completes as on the chip) and its SD card's busy line (each write ends without waiting for a timeout).
Build the sensor device and `athrill3` again from it:

```bash
cd "$HOME/athrill-hakoniwa"
git -C athrill-device fetch
git -C athrill-device checkout 6b6f588
cmake --build athrill-device/build-stm32 -j
cmake --build athrill-target-stm32/build --parallel
```

The firmware is the release [px4-v1.17.0-fmu-v6c](https://github.com/hakoniwalab/athrill-target-stm32/releases/tag/px4-v1.17.0-fmu-v6c):

```bash
cd "$HOME/athrill-hakoniwa"
curl -LO https://github.com/hakoniwalab/athrill-target-stm32/releases/download/px4-v1.17.0-fmu-v6c/px4_fmu-v6c_default.elf
curl -L -o SHA256SUMS-v6c https://github.com/hakoniwalab/athrill-target-stm32/releases/download/px4-v1.17.0-fmu-v6c/SHA256SUMS
shasum -a 256 -c SHA256SUMS-v6c
```

In the first terminal, the aircraft service with the x500 of `x500-athrill-px4`, as for the Pixhawk 4:

```bash
cd "$HOME/athrill-hakoniwa/hakoniwa-drone-core"
mac/mac-main_hako_aircraft_service_px4 127.0.0.1 4560 \
  config/drone/x500-athrill-px4 config/pdudef/drone-pdudef-athrill-px4.json --mujoco-viewer --real-sleep-msec 1
```

In the second, PX4 on the 6C, with motor 3 stopped at 40 s:

```bash
cd "$HOME/athrill-hakoniwa"
DYLD_LIBRARY_PATH=/usr/local/hakoniwa/lib athrill-target-stm32/examples/px4-v6c-hakoniwa-dshot/run.sh \
  px4_fmu-v6c_default.elf athrill-target-stm32/build/athrill3 athrill-device/build-stm32/libhako_sensor_device.dylib \
  motor3-stop.txt
```

The first run writes the x500 parameters, the 6C's sensor IDs and the DShot ones (`dshot-params.sh`:
`PWM_AUX_TIM0 -3`, `DSHOT_TEL_CFG 103`) into this example's own `fram.bin`. It runs the board with the sensor
device's test vehicle at rest where the x500 starts (`first-boot-config.txt`), as `examples/px4-hakoniwa` does, so
that PX4 stores its barometer offset for that place. Wait for `PX4 on the Pixhawk 6C with DShot and Hakoniwa`. In the
third:

```bash
cd "$HOME/athrill-hakoniwa"
/usr/local/hakoniwa/bin/hako-cmd start
python3 athrill-target-stm32/examples/px4-hakoniwa-dshot/fly-motor-fault.py
```

For a hover without the stop, leave out the fourth argument and use `examples/px4-hakoniwa/fly.py`. In process
(Drone PRO), give `HAKO_DRONE_ROOT` and `HAKO_AIRCRAFT_CONFIG_PATH` (`config/drone/x500-athrill-px4`) as above.

The first boot of `examples/px4-v6c-hakoniwa`, the 6C regression check, runs on the bench sensors (at rest at Tokyo
Station). Its parameters hold the barometer offset of that place, so an x500 flown with them holds about 2.7 m
instead of 2.5 m: it checks the arming and the take-off, not the hover.

### ArduCopter on the Pixhawk 6C

ArduCopter V4.7.2 `Pixhawk6C` with the same ESCs, example
[examples/ardupilot-v6c-hakoniwa-dshot](../examples/ardupilot-v6c-hakoniwa-dshot). It needs athrill-device `367619f`
or later and the firmware of [hakoniwa.md](hakoniwa.md#pixhawk-6c) (`ardupilot-6c/arducopter.elf`). In the first
terminal, the aircraft service with the ArduPilot airframe, as in [hakoniwa.md A2](hakoniwa.md#a2-fly). In the
second, with motor 3 stopped at 70 s:

```bash
cd "$HOME/athrill-hakoniwa"
printf 'DEVICE_CONFIG_ESC_MOTOR_STOP 2@70000\n' > motor3-stop-ap.txt
DYLD_LIBRARY_PATH=/usr/local/hakoniwa/lib athrill-target-stm32/examples/ardupilot-v6c-hakoniwa-dshot/run.sh \
  ardupilot-6c/arducopter.elf athrill-target-stm32/build/athrill3 athrill-device/build-stm32/libhako_sensor_device.dylib \
  motor3-stop-ap.txt
```

The first run writes the x500 parameters and `dshot.parm` (`MOT_PWM_TYPE 6`, `SERIAL5_PROTOCOL 16` for the ESC
telemetry on USART2, TELEM3) into this example's own `fram.bin`. Wait for
`ArduCopter on Athrill with DShot and Hakoniwa`. In the third:

```bash
cd "$HOME/athrill-hakoniwa"
/usr/local/hakoniwa/bin/hako-cmd start
python3 athrill-target-stm32/examples/ardupilot-hakoniwa-dshot/fly-motor-fault.py
```

For a hover without the stop, leave out the fourth argument and use `examples/ardupilot-hakoniwa/fly.py`.

## Results

2026-11-14, macOS (Apple M2 Pro), athrill3 from main with athrill-device `73a0f4d`, PX4 ELF of release v0.3.0,
ArduCopter V4.7.2.

**With the public Hakoniwa Drone v5.0.0** (aircraft service, the steps above, no viewer):
- PX4, `fly.py`: 3/3 PASS (RTF 1.25 to 1.36). In `esc_status` the four ESCs online, about 4,500 rpm and 6.2 A
  each while hovering (the model's telemetry); `motor_failure_mask` 0.
- PX4, motor 3 stopped at 40 s, `fly-motor-fault.py`: 3/3 PASS. `Motor failure detected` at 39.92 to 40.01 s of the
  vehicle's time, on the ground at 41.08 to 41.09 s; `motor_failure_mask` 4 (motor 3).
- ArduCopter, `examples/ardupilot-hakoniwa/fly.py`: 3/3 PASS (RTF 0.87 to 0.90).
- ArduCopter, motor 3 stopped at 70 s, `fly-motor-fault.py`: 3/3 PASS, on the ground at 71.09 s.
- These checks ran with other work on the machine, so their RTFs are not for comparison. PWM against DShot, measured
  earlier the same day on an otherwise idle machine, the same way (`fly.py`, 3 runs each):

| | PWM | DShot600 |
| --- | ---: | ---: |
| Interpreter | 1.51, 1.53, 1.54 | 1.43, 1.43, 1.43 |
| JIT and tick skip | 2.23, 2.43, 2.60 | 2.40, 2.40, 2.42 |

  The interpreter is about 6 % slower with DShot: each bit of each frame is a DMA transfer to the timer. With the JIT
  and tick skip the difference is within the spread of the runs.

**With the experimental build of Hakoniwa Drone PRO:**
- PX4 in process, motor 3 stopped at 40 s: 3/3 `Motor failure detected`, on the ground at 41.09 s. The ESC
  telemetry is the aircraft's: rpm and current of each rotor, and the stopped rotor's run-down.
- ArduCopter in process, motor 3 stopped at 70 s: 3/3 on the ground at 71.08 to 71.09 s, without a message about the
  motor (twice `Vibration compensation ON`). 364,840 DShot frames checked over a flight, all valid, at 1 kHz.
- Athrill as the Hakoniwa asset: the x500 in the browser at real time; a rotor set to 0 from the browser was
  detected by PX4 (`Motor failure detected`) and the x500 fell.

**Pixhawk 6C** (2026-11-14, athrill-device `6b6f588`, the ELF of release px4-v1.17.0-fmu-v6c):
- Public Hakoniwa Drone v5.0.0, aircraft service with `x500-athrill-px4`, no viewer: `fly.py` 3/3 PASS (2.48 to
  2.52 m in the last 10 s, RTF 0.86 to 0.88); motor 3 stopped at 40 s, `fly-motor-fault.py` 3/3 PASS
  (`Motor failure detected` at 39.95 s, on the ground at 40.90 to 40.91 s).
- Drone PRO experimental build, in process with `x500-athrill-px4`: `fly.py` PASS (2.49 to 2.50 m, RTF 0.90); motor 3
  stopped at 40 s, 3/3 PASS (on the ground at 40.89 s).

**ArduCopter on the Pixhawk 6C** (2026-11-14, athrill-device `a25171f` with the SD card change of its #124, ArduCopter
V4.7.2 `Pixhawk6C`):
- Public Hakoniwa Drone v5.0.0, aircraft service, motor 3 stopped at 70 s: `fly-motor-fault.py` 3/3 PASS, on the
  ground at 71.07 s (2.51 to 2.55 m before the stop).
- Drone PRO experimental build, in process: the same, PASS, on the ground at 71.02 s.

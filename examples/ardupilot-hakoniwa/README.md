# examples/ardupilot-hakoniwa

The official ArduCopter firmware flying the Hakoniwa Drone x500 on the emulated
Pixhawk 4 (see [docs/hakoniwa.md](../../docs/hakoniwa.md) and
[docs/dshot.md](../../docs/dshot.md)).

- `run.sh` — boot ArduCopter on Athrill. In process when `HAKO_DRONE_ROOT` and
  `HAKO_AIRCRAFT_CONFIG_PATH` are set; MAVLink is on TCP `127.0.0.1:5760`. It also
  honours `ATHRILL_OPTIONS` (passed to `athrill3` before the ELF), e.g.
  `--hakoniwa athrill --pdudef <file>` to make Athrill the Hakoniwa asset/conductor.
- `fly.py` — take off to 2.5 m and hold (a pass/fail hover check).
- `fly_and_attack.py` — verify an armed, stable hover, then run the
  `SERIAL_CONTROL` test using one MAVLink connection. An observed fall or reboot
  alone is **inconclusive**: the `DoS REPRODUCED` verdict additionally requires
  an IWDG underflow/reset diagnostic from Athrill. After a reset MAVLink altitude
  may go stale, so observe actual descent in the browser viewer.

## Browser viewer demo (`ardupilot-dos-viewer.launch.json`)

Athrill runs in process as the Hakoniwa asset/conductor and the in-process motor
sensor device publishes the x500's visual state; a PDU web bridge serves it to the
three.js viewer. The [Hakoniwa Launcher](https://github.com/hakoniwalab/hakoniwa-pdu-python)
starts the pieces in order and runs `hako-cmd start`.

The launch file's paths come from two environment variables:

- `HAKO_WS` — the directory that holds the repositories side by side
  (`athrill-target-stm32`, `hakoniwa-pdu-bridge-core`, `hakoniwa-threejs-drone`,
  `hakoniwalab-athrill-device-dshot`, `hakoniwa-drone-pro-motor-status`).
- `ARDUPILOT_ELF` — the ArduCopter ELF (the official `arducopter.elf` for Pixhawk 4).

Prerequisites (built once, outside this repo):

- `athrill3` with Hakoniwa and the JIT at `athrill-target-stm32/build-hakoniwa/athrill3`
  (`cmake -S . -B build-hakoniwa -DATHRILL_ENABLE_HAKONIWA=ON -DATHRILL_ENABLE_JIT=ON
  -DATHRILL_SLJIT_DIR=<sljit> -DATHRILL_SOURCE_DIR=<athrill core> -DATHRILL_DEVICE_DIR=<athrill-device>
  -DCMAKE_PREFIX_PATH=/usr/local/hakoniwa -DATHRILL_ADDRESS_WIDTH=32`).
  For a **confirmed** DoS verdict, build with an `athrill-device` that emits
  `IWDG_RESET: underflow cpu_clock=...` on an enabled watchdog reset
  (companion PR: [athrill-device #143](https://github.com/hakoniwalab/athrill-device/pull/143)).
- The in-process motor sensor plugin
  `libhako_sensor_device_inproc_motor.dylib` (macOS; `.so` on Linux — edit the path
  in the launch file) under `hakoniwalab-athrill-device-dshot/build-stm32-inproc`
  (`-DHAKO_SENSOR_DEVICE_BUILD_INPROC=ON -DHAKO_SENSOR_DEVICE_BUILD_INPROC_MOTOR=ON`).
- The PDU web bridge built in `hakoniwa-pdu-bridge-core/build`, and the three.js
  viewer checkout `hakoniwa-threejs-drone`.
- `fram.bin` present here (the first `run.sh` writes it; see docs/hakoniwa.md).

Run it:

```sh
export HAKO_WS=/path/to/workspace
export ARDUPILOT_ELF=/path/to/arducopter.elf
# clear any stale shared memory from a previous run
rm -f /var/lib/hakoniwa/mmap/mmap-0x*.bin /var/lib/hakoniwa/mmap/*.lock
python3 -m hakoniwa_pdu.apps.launcher.hako_launcher \
  "$HAKO_WS/athrill-target-stm32/examples/ardupilot-hakoniwa/ardupilot-dos-viewer.launch.json"
```

Then open the viewer and click **connect**:

```
http://127.0.0.1:8000/index.html?viewerConfigPath=/config/viewer-config-disturb-x500.json
```

Once MAVLink is up and the EKF is ready, in another terminal:

```sh
cd "$HAKO_WS/athrill-target-stm32/examples/ardupilot-hakoniwa"
python3 fly_and_attack.py --device 2
```

The script **aborts without attacking** if EKF, arming, takeoff, or stable hover
cannot be verified. The launcher redirects Athrill's stderr to
`logs/athrill.err`. The script reads only new `IWDG_RESET` events written after
the attack starts, then compares them with the reboot `STATUSTEXT` banner.
Use `--watchdog-log <path>` for a manual Athrill run with stderr redirected
elsewhere; without the diagnostic, a reboot or descent remains **INCONCLUSIVE**.
For a controlled no-attack check, pass `--attack-sec 0` and verify the script
never reports success. Exit codes: 0 = corroborated IWDG reset and reboot,
1 = no effect observed, 2 = inconclusive / prerequisite failed.

The x500 descends in the browser viewer after a confirmed FC reset; MAVLink
altitude can remain stale. Stop everything with Ctrl+C in the launcher terminal.

`viewer-extra.txt` is the extra board configuration for this demo (JIT, device tick
skip, the watchdog reset gate, and `HAKO_SENSOR_DEVICE_PUBLISH_ASSET`). The viewer
connects to the sim only while it is being flown; see the launcher notes if the
simulation time barely advances when nothing is driving it.

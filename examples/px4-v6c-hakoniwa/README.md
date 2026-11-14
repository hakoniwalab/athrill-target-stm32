# examples/px4-v6c-hakoniwa

The regression check of PX4 on the emulated Pixhawk 6C: an armed take-off with the in-process Hakoniwa aircraft,
compared value by value with athrill-device's reference (`boards/pixhawk6c/regression/armed.txt`):

```text
examples/px4-v6c-hakoniwa/verify-armed-inproc.sh PX4_ELF ATHRILL3 MODEL_PLUGIN HAKO_DRONE_ROOT AIRFRAME_CONFIG DTB WORK
```

It checks that the 6C boots, arms and takes off the same way each time:
- the instructions it runs;
- the console, the PWM and sensor traces;
- the F-RAM and the SD card;
- the order the devices tick in;
- that every console error or warning is explained (`console-check.py`).

To fly the 6C and check the hover, use [px4-v6c-hakoniwa-dshot](../px4-v6c-hakoniwa-dshot) (DShot ESCs; without
its fourth argument, a hover).

Two differences from the flight examples are intended, because the reference records them:
- **The first boot runs on the bench sensors** (the board's own sensor source, at rest at Tokyo Station), not on the
  sensor device's test vehicle where the x500 starts. The F-RAM it writes holds the barometer offset of that place,
  so an x500 flown with it holds about 2.7 m instead of 2.5 m. The check measures the arming and the take-off, not
  the hover height. `px4-v6c-hakoniwa-dshot` does its first boot on the test vehicle.
- **The aircraft is `x500-px4-sitl`** (Hakoniwa Drone's PX4 SITL x500, with `randomSeed` 12345), given as
  `AIRFRAME_CONFIG`. The flight examples use `x500-athrill-px4`. The reference was recorded with `x500-px4-sitl`; with
  another airframe the values differ. The script does not check the airframe's name.

The firmware is the release
[px4-v1.17.0-fmu-v6c](https://github.com/hakoniwalab/athrill-target-stm32/releases/tag/px4-v1.17.0-fmu-v6c); with
another build the values are not compared (`firmware_sha256` in the reference).

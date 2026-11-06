# Verification of PX4 SIH on the emulated Pixhawk 4

The evidence behind the README's claims, made by
[examples/px4-sih/verify/verify.sh](../examples/px4-sih/verify/verify.sh)
(one command, about 2 minutes) from the stock `px4_fmu-v5_default` ELF of
PX4 v1.17.0, the current stable release (and of PX4 `main` at `216039a`,
where marked). The charts come from the flight log (ULog) of its MAVLink
scenario, read from the emulated microSD card, with
[plot_ulog.py](../examples/px4-sih/verify/plot_ulog.py); the checks and their
thresholds are in [analyze_ulog.py](../examples/px4-sih/verify/analyze_ulog.py)
(chosen for this SIH vehicle, not PX4 requirements).

## What verify.sh does

```sh
examples/px4-sih/verify/verify.sh ../PX4/build/px4_fmu-v5_default/px4_fmu-v5_default.elf [athrill3]
```

It prints the ELF's SHA-256 (the results belong to that firmware), then:

1. **Golden images.** A first boot on a new F-RAM and a new 1 GB FAT32 SD
   card image selects the SIH airframe (`SYS_AUTOSTART 1100`). These two
   files are the starting state of every flight below: PX4 keeps
   parameters, logs and mission data in them, so a run that starts from a
   used image is a different experiment.
2. **A. Determinism.** The flight of [scenario.nsh](../examples/px4-sih/verify/scenario.nsh)
   (wait 12 s, `commander takeoff`, 20 s, `commander land`, 20 s) is typed
   into the NSH console, which Athrill delivers at fixed virtual times, so
   nothing depends on the host's clock. It runs three times at once from
   copies of the golden images, for 16 G clocks (74 s of virtual time):
   twice in the default mode, once in strict mode. The F-RAM and the whole
   SD card image must be byte for byte the same, and so must the console
   output, apart from the two `DEVICE_CONFIG_ARM_*` lines that Athrill
   echoes at start in the strict run (its configuration has them).
   Agreement with strict mode also shows that the fast paths (fast
   executors, host FPU) compute exactly what the Arm pseudo-code does.
3. **B. Checks of A's flight log.** The ULog is read from the SD card
   image (mtools, or hdiutil on macOS) and checked by
   [analyze_ulog.py](../examples/px4-sih/verify/analyze_ulog.py) (table
   below).
4. **C. A QGroundControl-style flight**, when pymavlink is installed: PX4
   runs with MAVLink on TCP, and
   [fly_scenario.py](../examples/px4-sih/verify/fly_scenario.py) arms,
   takes off, sends "go to" 5 m north (`MAV_CMD_DO_REPOSITION`), hovers
   15 s, lands and waits for the disarm, timing each phase by the
   vehicle's clock. Its log gets the same checks, plus holding the target.
   With matplotlib, [plot_ulog.py](../examples/px4-sih/verify/plot_ulog.py)
   draws the charts of this page from it.

The script ends with `ALL PASS` (exit status 0), `SOME CHECKS FAILED` (1),
or `PARTIAL PASS` (2) when pymavlink is missing and C was skipped; install
the Python packages with `pip install numpy pyulog pymavlink matplotlib`. Its files stay in `examples/px4-sih/verify/work/`: `golden/`,
`fast1/`, `fast2/`, `strict/` and `mavlink/` (each with its console output,
F-RAM and SD card image), `a.ulg`, `mavlink/c.ulg` and `charts/`.

For a shorter equivalence regression between the legacy board path and the
Devicetree path, run:

```sh
examples/px4-sih/verify/compare-path-counts.sh px4_fmu-v5_default.elf pixhawk4.dtb [athrill3]
```

It boots the SIH airframe once and runs both paths from cloned images for 1.5
G clocks. Their instruction counts, normalized consoles, F-RAM and SD images
must match. The window includes the GPIO-read/WFI sequence that previously
made the DT path report extra CPU steps.

### The checks of a flight log

The thresholds are for this SIH quadcopter, chosen to catch a broken
flight, not PX4 requirements. "Flying" is from takeoff to touchdown as
`vehicle_land_detected` reports them.

| Check | Topics | Passes when |
| --- | --- | --- |
| log | the ULog | no dropouts |
| flight | `vehicle_status`, `vehicle_land_detected` | armed, airborne, landed again, disarmed at the end |
| ekf2 faults | `estimator_status` | `filter_fault_flags` always 0 |
| ekf2 innovations | `estimator_status` | position, velocity, height and heading test ratios below 1 (inside PX4's gates) while armed |
| ekf2 position vs truth | `vehicle_local_position`, `…_groundtruth` | while flying, horizontal error RMS below 0.6 m and max below 1.5 m, vertical RMS below 0.3 m and max below 1.0 m (wide enough for v1.17.0's noisier simulated GPS, below) |
| ekf2 tilt vs truth | `vehicle_attitude`, `…_groundtruth` | tilt error RMS below 0.6°, max below 2° (the two topics are sampled at different times, so fast attitude changes show a lag; with PX4 v1.17.0 builds RMS 0.42–0.50°, and 0.67° in one run on 2026-11-04 that started from other F-RAM/SD images and could not be reproduced: its three runs were identical to each other, at 2,905,752,890 instructions) |
| ekf2 heading vs truth | the same | heading error below 5° |
| attitude tracking | `vehicle_attitude`, `vehicle_attitude_setpoint` | tilt error RMS below 2° while flying |
| altitude hold | `vehicle_local_position` | from reaching 2.5 m (within 5 cm) to the descent: mean within 5 cm of 2.5 m, every sample within 15 cm |
| position hold | `vehicle_local_position` | A: horizontal drift while hovering below 0.6 m; C: within 0.6 m of the target over the last 5 s |

### What it does not show

- **PX4's sensor drivers.** SIH gives PX4 its sensor data directly; the
  emulated chips are not in this data path (README, "Beyond SIH").
- **Timing of the real board.** Every instruction takes one clock of the
  virtual 216 MHz; PX4's CPU load and its real-time margins on a Pixhawk
  are not modelled.
- **Identity across hosts.** Each host's runs are identical, but different
  ELFs give different instruction counts; one ELF on two hosts is not
  compared here.
- **Identity of MAVLink runs.** C's commands arrive at wall-clock times,
  so its runs differ in detail; they are judged by their logs only.

## The flight

Arm, take off to the default 2.5 m, "go to" a point 5 m north (`MAV_CMD_DO_REPOSITION`,
what QGroundControl sends for "Go to location"), hover, land, disarm: all over
MAVLink on TCP, as QGroundControl does ([fly_scenario.py](../examples/px4-sih/verify/fly_scenario.py)).
SIH logs its own ground truth next to the EKF2 estimate. The charts are of
**PX4 v1.17.0**, the current stable release.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="verification/altitude-dark.png">
  <img alt="Altitude over time: setpoint, SIH ground truth and EKF2 estimate within about 0.1 m of each other from takeoff through the 2.5 m hover to touchdown" src="verification/altitude-light.png" width="720">
</picture>

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="verification/path-dark.png">
  <img alt="Path seen from above: from the start to the target 5 m north; the estimate runs about 0.3 m west of the ground truth" src="verification/path-light.png" width="420">
</picture>

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="verification/tilt-dark.png">
  <img alt="Tilt over time: the vehicle follows the setpoint, up to 20 degrees when it starts towards the target" src="verification/tilt-light.png" width="720">
</picture>

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="verification/ekf2-ratios-dark.png">
  <img alt="EKF2 innovation test ratios for position, velocity, height and heading stay below 0.3, far under the gate of 1" src="verification/ekf2-ratios-light.png" width="720">
</picture>

| Check (MAVLink scenario) | Mac, PX4 v1.17.0 | WSL2, PX4 v1.17.0 | Mac, PX4 main `216039a` | WSL2, PX4 main `216039a` |
| --- | --- | --- | --- | --- |
| Commands acknowledged (arm, takeoff, reposition, land) | all | all | all | all |
| EKF2 filter faults | none | none | none | none |
| EKF2 innovation test ratios, max (position, velocity, height, heading) | 0.11, 0.14, 0.05, 0.21 | below 1 (PASS) | 0.02, 0.05, 0.18, 0.15 | below 1 (PASS) |
| EKF2 position vs ground truth, RMS horizontal / vertical (max) | 46 cm / 12 cm (92 cm / 37 cm) | PASS | 5.1 cm / 2.2 cm | 5.0 cm horizontal |
| Tilt tracking error, RMS (max tilt) | 1.07° (20.1°) | PASS | 1.19° (21.2°) | 1.20° (21.2°) |
| Hover altitude, mean (target 2.5 m) | 2.511 m | 2.511 m | 2.490 m | 2.489 m |
| Distance to the target over the last 5 s, mean / max | 33 cm / 40 cm | 33 cm / 40 cm | 13 cm / 18 cm | 13 cm / 18 cm |

The EKF2 errors differ between the PX4 versions because SIH's simulated GPS
does: v1.17.0 adds white noise to every fix (0.2 m per horizontal axis,
0.5 m vertical), `main` a correlated noise model. Both versions run the same
way in strict mode, so this is PX4, not the emulator.

With v1.17.0, PX4 also republishes its home position during the flight
(60 times on the Mac, 82 on WSL2), its altitude moving by up to about 2.5 m
with the GPS noise. The altitude above home that MAVLink reports
(`GLOBAL_POSITION_INT.relative_alt`, shown by QGroundControl) therefore
jumps between about 1.3 m and 2.4 m while the vehicle holds 2.5 m in
EKF2's local frame and in SIH's ground truth; with `main` it stays at
2.49–2.51 m. Not yet investigated further on the PX4 side.

After touchdown, SIH's ground truth ends up to 0.5 m below the takeoff point
(0.1 m in this v1.17.0 flight) and the estimate settles back to 0: SIH's
simple ground model (`sih.cpp`, "fake ground") stops the vehicle only once
it is below the reference altitude. This too is PX4's SIH.

## Determinism

The same flight typed into the NSH console at fixed virtual times
([scenario.nsh](../examples/px4-sih/verify/scenario.nsh): take off, hover,
land), three times from the same F-RAM and SD card images, in parallel:
twice in the default mode, once in strict mode (`DEVICE_CONFIG_ARM_FP_MODE`
and `DEVICE_CONFIG_ARM_EXEC_MODE strict`: Arm pseudo-code only, no fast
executors, no host FPU). PX4 v1.17.0 on the Mac:

| Run | Instructions | SD card image after the run, SHA-256 | ULog, SHA-256 | F-RAM, SHA-256 | Console without the mode lines, SHA-256 |
| --- | ---: | --- | --- | --- | --- |
| fast 1 | 2,903,884,344 | `cdce0b68427e967b…` | `5aaa66ebf76d7fde…` | `c1a69ccb3740c797…` | `36a05910bf2ca418…` |
| fast 2 | 2,903,884,344 | `cdce0b68427e967b…` | `5aaa66ebf76d7fde…` | `c1a69ccb3740c797…` | `36a05910bf2ca418…` |
| strict | 2,903,884,344 | `cdce0b68427e967b…` | `5aaa66ebf76d7fde…` | `c1a69ccb3740c797…` | `36a05910bf2ca418…` |

The whole 1 GB SD card image is the same bit for bit, and the fast paths
give exactly the pseudo-code's results over the whole flight. The blank
SD card image is written by `make-sd-image.sh` the same on every host
(python3, the macOS `newfs_msdos` layout with a fixed serial). Formatting
it with the host's tools does not do: `mkfs.vfat` on Linux chooses another
FAT size, and PX4 then ran 2,903,919,925 instructions instead of
2,903,884,344 with the same ELF. With the PX4 ELF of the Release (the one in
this table), `verify.sh` compares the four results with these published
values (check D), so a run on your computer can be shown to be identical
to ours, bit for bit. **Across hosts:** on WSL2 (Intel Core Ultra 7 155H),
the release ELF on the Linux x86_64 athrill3 of the Release gave the same
2,903,884,344 instructions and passed check D: its SD card image, ULog,
F-RAM and console are identical to the Mac's (Apple M2 Pro). Every host and version below gave three identical runs. The counts
belong to the ELF each host built (`verify.sh` prints its SHA-256 first):

| Host | PX4 | `px4_fmu-v5_default.elf`, SHA-256 | Instructions of each run |
| --- | --- | --- | ---: |
| Mac (arm64 build image) | v1.17.0 | `f8a78a13f3a545305cce45eeef677e4d436325ee6a6065b1753c387283f3d05e` | 2,903,884,344 |
| Mac (arm64 build image) | main `216039a` | `3b1e760d380493539212d1d414612aabbe7ef2e65197bfe83215caad8c11c482` | 3,507,861,181 |
| WSL2 (amd64 build image) | v1.17.0 | `0bb9d54bf79d3444c287716043c0e10dece765b855b73730234d68cf3683eb55` | 2,903,946,936 |
| WSL2 (amd64 build image) | main `216039a` | `2a8a47df1f67c58a3d97de05716c2952a409f080eede6f67d16b1ce0a37c7e72` | 3,507,214,657 |

Different ELFs (PX4 embeds build information, and the images differ) give
different counts, so the counts are not compared across them.

Input timed by the wall clock, such as QGroundControl's commands over TCP,
arrives at run-dependent virtual times: those runs are checked by their
logs, not for identity.

## QGroundControl

On the Mac and on Windows (QGroundControl on Windows, PX4 on Athrill in
WSL2, TCP link to `127.0.0.1:5760` through WSL's localhost forwarding), the
vehicle shows at PX4's SIH home in Zurich and is armed, taken off, moved
and landed from QGroundControl by hand.

## Test machines

| | Mac | WSL2 PC |
| --- | --- | --- |
| Machine | MacBook Pro (Mac14,9) | MouseComputer S4I7G60SRDDC (notebook), on AC, Windows power plan "High performance" |
| CPU | Apple M2 Pro, 12 cores (8 performance, 4 efficiency) | Intel Core Ultra 7 155H, 16 cores / 22 threads |
| Memory | 32 GB | 63.4 GB (WSL2 sees 31 GB, the default half) |
| OS | macOS 27.2 | Windows 10.0.26300.9711, WSL 2.4.13.0 (kernel 5.15.167.4), Ubuntu 24.04.4 LTS |
| Compiler, CMake | Apple clang 21.0.0, CMake 4.4.3 | gcc 13.3.0, CMake 3.28.3 |
| PX4 build | Docker, `px4io/px4-dev:v1.17.0-beta1` (arm64) | Docker 29.8.2 inside WSL2, same image (amd64) |

## Speed

RTF is virtual time over wall-clock time (above 1: faster than real time);
Athrill runs on one host core. All runs in the default fast mode unless
marked strict.

| Workload | Mac, PX4 v1.17.0 | WSL2, PX4 v1.17.0 | Mac, PX4 main | WSL2, PX4 main |
| --- | ---: | ---: | ---: | ---: |
| SIH hover over MAVLink, SD logging (`run.sh` + pymavlink) | 2.22 | 1.32 | 1.92 | 1.29 (1.34 just after the GPS fix) |
| NSH flight of verify.sh A, boot to disarm, 74 s of virtual time, one run alone | 2.25 (32.9 s) | 1.36 (54.3 s) | 2.01 (36.8 s) | 1.34 (55.5 s) |
| The same, strict mode (Arm pseudo-code only) | 1.80 (41.1 s) | 1.09 (67.9 s) | 1.58 (46.8 s) | 1.06 (70.1 s) |
| `verify.sh`, everything (A runs its 3 flights in parallel) | about 100 s | 155 s | about 100 s | 159 s |
| Build: Athrill (cmake, build, ctest) | not timed | not timed | not timed | 44 s |
| Build: PX4 (clone / Docker make) | 356 s together (shallow clone of the tag, no separate submodule step) | 38 s / 215 s (shallow clone of the tag; image already pulled) | not timed | 266 s / 394 s (with submodules and the image pull) |

PX4 v1.17.0 runs about 17 % fewer instructions than `main` for the same
flight, hence its higher RTF. Strict mode takes about 1.25 times as long as
the fast mode. On the WSL2 PC something else kept one core busy during
these runs (load average 1.00 before them); Athrill uses one core, so the
effect should be small.

Hakoniwa Drone PRO's x500 (README, "Beyond SIH") runs at RTF 1.48 on the
Mac, the whole system with MuJoCo and no viewer.

# Verification of PX4 SIH on the emulated Pixhawk 4

The evidence behind the README's claims, made by
[examples/px4-sih/verify/verify.sh](../examples/px4-sih/verify/verify.sh)
(one command, about 2 minutes) from the stock `px4_fmu-v5_default` ELF of
PX4 `216039a`. The charts come from the flight log (ULog) of its MAVLink
scenario, read from the emulated microSD card, with
[plot_ulog.py](../examples/px4-sih/verify/plot_ulog.py); the checks and their
thresholds are in [analyze_ulog.py](../examples/px4-sih/verify/analyze_ulog.py)
(chosen for this SIH vehicle, not PX4 requirements).

## The flight

Arm, take off to the default 2.5 m, "go to" a point 5 m north (`MAV_CMD_DO_REPOSITION`,
what QGroundControl sends for "Go to location"), hover, land, disarm: all over
MAVLink on TCP, as QGroundControl does ([fly_scenario.py](../examples/px4-sih/verify/fly_scenario.py)).
SIH logs its own ground truth next to the EKF2 estimate.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="verification/altitude-dark.png">
  <img alt="Altitude over time: setpoint, SIH ground truth and EKF2 estimate overlap from takeoff through the 2.5 m hover to touchdown" src="verification/altitude-light.png" width="720">
</picture>

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="verification/path-dark.png">
  <img alt="Path seen from above: from the start point straight to the target 5 m north, ground truth and estimate overlapping" src="verification/path-light.png" width="420">
</picture>

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="verification/tilt-dark.png">
  <img alt="Tilt over time: the vehicle follows the setpoint, up to 21 degrees when it starts towards the target" src="verification/tilt-light.png" width="720">
</picture>

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="verification/ekf2-ratios-dark.png">
  <img alt="EKF2 innovation test ratios for position, velocity, height and heading stay below 0.2, far under the gate of 1" src="verification/ekf2-ratios-light.png" width="720">
</picture>

| Check (MAVLink scenario) | Apple Silicon Mac | WSL2, Intel Core Ultra 7 155H |
| --- | --- | --- |
| Commands acknowledged (arm, takeoff, reposition, land) | all | all |
| EKF2 filter faults | none | none |
| EKF2 innovation test ratios, max (position, velocity, height, heading) | 0.02, 0.05, 0.18, 0.15 | below 1 (PASS) |
| EKF2 position vs ground truth, RMS (horizontal, vertical) | 5.1 cm, 2.2 cm | 5.0 cm |
| Tilt tracking error, RMS (max tilt) | 1.19° (21.2°) | 1.20° (21.2°) |
| Hover altitude, mean (target 2.5 m) | 2.490 m | 2.489 m |
| Distance to the target over the last 5 s, mean / max | 13 cm / 18 cm | 13 cm / 18 cm |

After touchdown, SIH's ground truth ends 0.2 to 0.5 m below the takeoff
point and the estimate settles back to 0: SIH's simple ground model
(`sih.cpp`, "fake ground") stops the vehicle only once it is below the
reference altitude. This is PX4's SIH, computed the same in strict mode
(below), not the emulator.

## Determinism

The same flight typed into the NSH console at fixed virtual times
([scenario.nsh](../examples/px4-sih/verify/scenario.nsh): take off, hover,
land), three times from the same F-RAM and SD card images, in parallel:
twice in the default mode, once in strict mode (`DEVICE_CONFIG_ARM_FP_MODE`
and `DEVICE_CONFIG_ARM_EXEC_MODE strict`: Arm pseudo-code only, no fast
executors, no host FPU).

| Run (Mac) | Instructions | SD card image after the run, SHA-256 | F-RAM, SHA-256 | Console, SHA-256 |
| --- | ---: | --- | --- | --- |
| fast 1 | 3,507,861,181 | `95ce250e9d41b178…` | `8972ebd62a8ca8e2…` | `82900639c8396631…` |
| fast 2 | 3,507,861,181 | `95ce250e9d41b178…` | `8972ebd62a8ca8e2…` | `82900639c8396631…` |
| strict | 3,507,861,181 | `95ce250e9d41b178…` | `8972ebd62a8ca8e2…` | `82900639c8396631…` |

The whole 1 GB SD card image, with the ULog, is the same bit for bit, and
the fast paths give exactly the pseudo-code's results over the whole
flight. On WSL2 the three runs are identical too. The counts belong to the
ELF each host built (`verify.sh` prints its SHA-256 first):

| Host | `px4_fmu-v5_default.elf`, SHA-256 | Instructions of each run |
| --- | --- | ---: |
| Apple Silicon Mac (arm64 build image) | `3b1e760d380493539212d1d414612aabbe7ef2e65197bfe83215caad8c11c482` | 3,507,861,181 |
| WSL2, Intel Core Ultra 7 155H (amd64 build image) | `2a8a47df1f67c58a3d97de05716c2952a409f080eede6f67d16b1ce0a37c7e72` | 3,507,214,657 |

The two ELFs differ (PX4 embeds build information, and the images differ),
so their counts are not compared with each other.

Input timed by the wall clock, such as QGroundControl's commands over TCP,
arrives at run-dependent virtual times: those runs are checked by their
logs, not for identity.

## QGroundControl

On the Mac and on Windows (QGroundControl on Windows, PX4 on Athrill in
WSL2, TCP link to `127.0.0.1:5760` through WSL's localhost forwarding), the
vehicle shows at PX4's SIH home in Zurich and is armed, taken off, moved
and landed from QGroundControl by hand.

## Speed

| Host | RTF while hovering (MAVLink, SD logging) | verify.sh |
| --- | ---: | ---: |
| Apple Silicon Mac | 1.92 | about 100 s |
| WSL2, Intel Core Ultra 7 155H | 1.29–1.34 | 159 s |

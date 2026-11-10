# Related work

A survey of public work, as of 2026-11-05, on running flight-controller firmware on an emulator in closed loop with
physics. It is here so that what this repository claims stays accurate. "Not found" means not found in public
sources: in-house work cannot be checked.

## Closest: ArduPilot on Renode

[ArduPilot's Renode emulation](https://ardupilot.org/dev/docs/renode-emulation.html), from upstream ArduPilot
(ArduPilot PRs #33975, merged 2026-09-05, and #34785, merged 2026-10-17):
- unmodified ChibiOS firmware source for many STM32 boards (F1 to H7), with peripherals generated from the hwdef:
  timers, DMA, SPI/I2C/UART, IMUs (ICM20689 among them), MS5611, IST8310, u-blox;
- a physics sidecar in lockstep with Renode's virtual time, using ArduPilot's own SITL dynamics. A QuadPlane
  (CubeOrangePlus) flies a mission in CI; a Copter scenario (KakuteF4) runs locally;
- the flight tests use firmware built from source with embedded default parameters; Renode paces to real time by
  default. A real-time factor in flight is not published.

So, for ArduPilot, emulated firmware flying in closed loop with physics, through emulated sensor chips, is **not new**.
What differs here:
- the **official binary** from firmware.ardupilot.org (ArduCopter V4.7.2, `Pixhawk4`), not a local build;
- **external, general-purpose physics** (MuJoCo, the Hakoniwa Drone x500), not ArduPilot's own SITL dynamics;
- **faster than real time**: RTF 1.16 to 1.26 while hovering (Apple M2 Pro, `DEVICE_CONFIG_SPIN_HALT`). On the
  same ELF without physics and with the same IMU, Athrill runs 1.24 (interpreter) and 2.27 (JIT and tick skip)
  against Renode's 0.60 ([ardupilot.md](ardupilot.md));
- the same emulator, physics and lockstep also fly **PX4** (below).

## PX4

Not found: unmodified PX4 board firmware (`px4_fmu-v5_default` here) run on an instruction-set emulator, reading
emulated sensor chips with PX4's own drivers, in closed loop with external physics. The PX4 repository has no
Renode or QEMU issues or pull requests for this.

Related, not the same:
- **PX4 on AIFoundry Erbium** (upstream PX4 PR #29094, merged 2026-10): `aifoundry_erbium_default` on the Erbium
  emulator, holding 2.5 m with **SIH**, PX4's built-in physics. That target has no sensor or actuator drivers.
  Athrill RISC-V's Hakoniwa variant adds a virtual sensor device and its PX4 driver, so it is not unmodified PX4.
- **PX4 HITL, PX4 SIH on hardware, ArduPilot Sim on Hardware, MATLAB/Simulink PX4 PIL**: these run on real boards.

## Others

| Work | What runs | Closed loop with physics | Notes |
| --- | --- | --- | --- |
| Crazyflie 2.1 on Renode (Lund University thesis, 2021) | Crazyflie firmware, STM32F405 | No: flying was out of scope | [bitcraze.io](https://www.bitcraze.io/2021/04/successful-emulation/) |
| Antmicro RAMN + CARLA (2026-08) | four automotive ECUs (STM32L5) | Yes, with CARLA; not a drone | [antmicro.com](https://antmicro.com/blog/2026/08/toyota-ramn-and-carla-in-renode) |
| Firmware rehosting (P2IM, Fuzzware, HALucinator, Jetset ...) | general MCU firmware | No flight reported; aimed at fuzzing | |
| PGFuzz (NDSS 2021) | ArduPilot/PX4 **SITL** with Gazebo | Yes, but SITL, not emulation | |
| RoSÉ (ISCA 2023) | RISC-V SoC RTL on FireSim, lockstep with AirSim | Yes; companion computer, not a flight controller | |
| Arm Fast Models, Synopsys, Cadence, Simics, Imperas, dSPACE | Cortex-M models exist | No public PX4/ArduPilot flight case found | |

## What we claim

- Within what we found: unmodified **PX4** board firmware flies on an instruction-set emulator through its own
  sensor drivers, in closed loop with external physics.
- Within what we found: the same setup flies **both** the official ArduPilot binary and unmodified PX4 board
  firmware, faster than real time.
- PX4 SIH runs identical, bit for bit, on macOS and Linux ([verification.md](verification.md)). With SIH the physics
  is inside the firmware, so this is a separate claim from the closed loop above.

Not claimed: that emulated firmware flying with physics is new, that Athrill is the only lockstep emulator, or that
the first PX4 on Erbium is ours.

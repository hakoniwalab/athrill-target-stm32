# attack-serial-control.py

A MAVLink `SERIAL_CONTROL` flood for **authorized** security testing of our own
ArduPilot on localhost (SITL or PILS). It reproduces the denial-of-service from
Soshima & Suzaki's ROHITL paper: a `SERIAL_CONTROL` with a long timeout makes
ArduPilot spin in `hal.scheduler->delay(1)` (`GCS_serial_control.cpp`), stalling
`main_loop`. The paper's point is that SITL cannot show the effect but the real
RTOS can; the same script drives both — only the URL changes.

The script only drives the attack and reports whether ArduPilot still answers; it
does not decide whether the vehicle crashed. That is observed in the environment:
PILS resets through the IWDG (run it with `DEVICE_CONFIG_IWDG_RESET=1`), SITL does
not.

## Against the ArduPilot SITL (MuJoCo)

Start the SITL (hakoniwa-drone-pro `tools/launch-mujoco-ardupilot-sitl.bash`), then:

```
examples/attack-serial-control.py tcp:127.0.0.1:5760 --timeout-ms 3000 --rate 50 --duration 10
```

Expected: ArduPilot keeps flying — SITL cannot reproduce the stall.

## Against the ArduPilot PILS (Athrill)

Fly ArduPilot on Athrill (`examples/ardupilot-hakoniwa`), with the IWDG reset
enabled in its device configuration (`DEVICE_CONFIG_IWDG_RESET 1`), then run the
same command against that MAVLink endpoint.

Expected: `main_loop` stalls, the IWDG is not kicked, and it resets the MCU — the
paper's result, with no hardware.

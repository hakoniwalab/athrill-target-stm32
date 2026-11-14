#!/usr/bin/env python3
"""Judge the ArduPilot bench from its MAVLink capture (run-full.sh, 20 virtual s).

    examples/ardupilot-bench/check-bench.py examples/ardupilot-bench/mavlink-full.raw

PASS when ArduCopter, with its own drivers on the emulated chips, reports:
- at least 10 HEARTBEATs from system 1 (a quadrotor, ArduPilot);
- the start-up milestones: "ArduPilot Ready", "EKF3 IMU0 initialised", "GPS 1: detected u-blox";
- the gyroscope, accelerometer, compass and barometer healthy in SYS_STATUS;
- a 3D GPS fix (GPS_RAW_INT fix_type 3 or more; its SYS_STATUS health comes later than 20 s);
- RAW_IMU up to 15 s of the vehicle's time or later: the MAVLink output did not stop
  (athrill-device#102 stopped a board's MAVLink TX after a few seconds).
"""
import os
import sys

os.environ["MAVLINK20"] = "1"
from pymavlink import mavutil  # noqa: E402

MILESTONES = ("ArduPilot Ready", "EKF3 IMU0 initialised", "GPS 1: detected u-blox")
HEALTHY = ("3D_GYRO", "3D_ACCEL", "3D_MAG", "ABSOLUTE_PRESSURE")
MIN_HEARTBEATS = 10
LAST_IMU_SEC = 15.0


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    link = mavutil.mavlink_connection(sys.argv[1], dialect="ardupilotmega", notimestamps=True)
    heartbeats = 0
    texts = set()
    status = None
    fix = 0
    last_imu = 0.0
    while True:
        m = link.recv_match(blocking=False)
        if m is None:
            break
        kind = m.get_type()
        if kind == "HEARTBEAT":
            if (m.get_srcSystem() == 1 and m.type == mavutil.mavlink.MAV_TYPE_QUADROTOR
                    and m.autopilot == mavutil.mavlink.MAV_AUTOPILOT_ARDUPILOTMEGA):
                heartbeats += 1
        elif kind == "STATUSTEXT":
            texts.add(m.text.rstrip("\x00"))
        elif kind == "SYS_STATUS":
            status = m
        elif kind == "GPS_RAW_INT":
            fix = max(fix, m.fix_type)
        elif kind == "RAW_IMU":
            last_imu = max(last_imu, m.time_usec / 1e6)
    failures = []
    if heartbeats < MIN_HEARTBEATS:
        failures.append(f"{heartbeats} HEARTBEATs (want {MIN_HEARTBEATS} or more)")
    for text in MILESTONES:
        if not any(t.startswith(text) for t in texts):
            failures.append(f"no STATUSTEXT '{text}'")
    if status is None:
        failures.append("no SYS_STATUS")
    else:
        for name in HEALTHY:
            bit = getattr(mavutil.mavlink, "MAV_SYS_STATUS_SENSOR_" + name)
            if not status.onboard_control_sensors_health & bit:
                failures.append(f"{name} not healthy")
    if fix < 3:
        failures.append(f"GPS fix_type {fix} (want 3 or more)")
    if last_imu < LAST_IMU_SEC:
        failures.append(f"MAVLink stopped: last RAW_IMU at {last_imu:.1f} s (want {LAST_IMU_SEC:.0f} s or later)")
    print(f"{heartbeats} HEARTBEATs, GPS fix {fix}, last RAW_IMU at {last_imu:.1f} s")
    for f in failures:
        print(f"  [FAIL] {f}")
    if failures:
        return 1
    print("PASS: ArduCopter booted, its sensors healthy, MAVLink until the end")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Validate and summarize ArduCopter v6C sensor status and MAVLink data."""

import math
import sys
from collections import Counter

from pymavlink import mavutil


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} mavlink.raw", file=sys.stderr)
        return 2
    link = mavutil.mavlink_connection(sys.argv[1], dialect="ardupilotmega", notimestamps=True)
    counts = Counter()
    texts = set()
    latest_status = None
    latest_imu = None
    latest_pressure = None
    params = {}
    while True:
        message = link.recv_match(blocking=False)
        if message is None:
            break
        kind = message.get_type()
        if kind == "BAD_DATA":
            continue
        counts[kind] += 1
        if kind == "STATUSTEXT":
            texts.add(message.text.rstrip("\x00"))
        elif kind == "SYS_STATUS":
            latest_status = message
        elif kind == "RAW_IMU":
            latest_imu = message
        elif kind == "SCALED_PRESSURE":
            latest_pressure = message
        elif kind == "PARAM_VALUE":
            params[message.param_id.rstrip("\x00")] = message.param_value

    required_text = (
        "Barometer 1 calibration complete",
        "EKF3 IMU0 initialised",
        "EKF3 IMU1 initialised",
        "EKF3 IMU0 MAG0 initial yaw alignment complete",
    )
    missing = [text for text in required_text if text not in texts]
    if missing:
        raise SystemExit(f"FAIL: missing STATUSTEXT: {', '.join(missing)}")
    if latest_status is None or latest_imu is None or latest_pressure is None:
        raise SystemExit("FAIL: SYS_STATUS, RAW_IMU or SCALED_PRESSURE absent")
    required_bits = (
        mavutil.mavlink.MAV_SYS_STATUS_SENSOR_3D_GYRO
        | mavutil.mavlink.MAV_SYS_STATUS_SENSOR_3D_ACCEL
        | mavutil.mavlink.MAV_SYS_STATUS_SENSOR_3D_MAG
        | mavutil.mavlink.MAV_SYS_STATUS_SENSOR_ABSOLUTE_PRESSURE
    )
    for field in ("onboard_control_sensors_present", "onboard_control_sensors_health"):
        if getattr(latest_status, field) & required_bits != required_bits:
            raise SystemExit(f"FAIL: {field} lacks required sensor bits")
    acceleration_g = math.sqrt(latest_imu.xacc ** 2 + latest_imu.yacc ** 2 + latest_imu.zacc ** 2) / 1000.0
    gyro = math.sqrt(latest_imu.xgyro ** 2 + latest_imu.ygyro ** 2 + latest_imu.zgyro ** 2) / 1000.0
    if not 0.9 <= acceleration_g <= 1.1 or gyro > 0.1:
        raise SystemExit(f"FAIL: RAW_IMU acceleration={acceleration_g}g gyro={gyro}rad/s")
    if not 800.0 <= latest_pressure.press_abs <= 1200.0:
        raise SystemExit(f"FAIL: pressure={latest_pressure.press_abs}hPa")
    for name in ("INS_GYR_ID", "INS_GYR2_ID", "BARO1_DEVID", "COMPASS_DEV_ID"):
        if params.get(name, 0.0) == 0.0:
            raise SystemExit(f"FAIL: {name} absent or zero")

    print("PASS: ArduCopter v6C sensors found and live")
    print(f"messages: RAW_IMU={counts['RAW_IMU']} SCALED_PRESSURE={counts['SCALED_PRESSURE']} SYS_STATUS={counts['SYS_STATUS']}")
    print(f"raw_imu: {latest_imu.xacc} {latest_imu.yacc} {latest_imu.zacc} mg; "
          f"{latest_imu.xgyro} {latest_imu.ygyro} {latest_imu.zgyro} mrad/s")
    print(f"pressure: {latest_pressure.press_abs:.4f} hPa temperature={latest_pressure.temperature / 100.0:.4f} C")
    print("device_ids: " + " ".join(f"{name}={params[name]:.0f}" for name in
          ("INS_GYR_ID", "INS_GYR2_ID", "BARO1_DEVID", "COMPASS_DEV_ID")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

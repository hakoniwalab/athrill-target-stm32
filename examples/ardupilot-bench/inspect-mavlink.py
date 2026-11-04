#!/usr/bin/env python3
"""Print ArduPilot status text and sensor status from a raw UART capture."""

import argparse
import sys
from collections import Counter

from pymavlink import mavutil


def sensor_names(mask):
    enum = mavutil.mavlink.enums["MAV_SYS_STATUS_SENSOR"]
    return [value.name.removeprefix("MAV_SYS_STATUS_SENSOR_")
            for bit, value in sorted(enum.items())
            if bit and bit & (bit - 1) == 0 and mask & bit]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture")
    args = parser.parse_args()
    link = mavutil.mavlink_connection(
        args.capture, dialect="ardupilotmega", notimestamps=True)
    counts = Counter()
    status_text = []
    seen_text = set()
    latest_status = None
    latest_gps = None
    parameters = {}
    while True:
        message = link.recv_match(blocking=False)
        if message is None:
            break
        name = message.get_type()
        if name == "BAD_DATA":
            continue
        counts[name] += 1
        if name == "STATUSTEXT":
            text = message.text.rstrip("\x00")
            if text not in seen_text:
                seen_text.add(text)
                status_text.append(text)
        elif name == "SYS_STATUS":
            latest_status = message
        elif name == "GPS_RAW_INT":
            latest_gps = message
        elif name == "PARAM_VALUE":
            parameters[message.param_id.rstrip("\x00")] = message.param_value

    print("STATUSTEXT:")
    for text in status_text:
        print(f"  {text}")
    if latest_status is None:
        print("SYS_STATUS: absent")
    else:
        print("SYS_STATUS sensors:")
        for field in ("onboard_control_sensors_present",
                      "onboard_control_sensors_enabled",
                      "onboard_control_sensors_health"):
            names = sensor_names(getattr(latest_status, field))
            print(f"  {field.removeprefix('onboard_control_sensors_')}: "
                  f"{', '.join(names) if names else 'none'}")
    if latest_gps is not None:
        print(f"GPS_RAW_INT: fix_type={latest_gps.fix_type}, "
              f"satellites_visible={latest_gps.satellites_visible}")
    if parameters:
        print("sensor parameters:")
        for name, value in sorted(parameters.items()):
            print(f"  {name}: {value:g}")
    interesting = ("HEARTBEAT", "SYS_STATUS", "GPS_RAW_INT", "RAW_IMU",
                   "SCALED_PRESSURE", "SCALED_PRESSURE2", "SCALED_PRESSURE3")
    print("message counts:")
    for name in interesting:
        if counts[name]:
            print(f"  {name}: {counts[name]}")
    return 0 if latest_status is not None else 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Check a raw SERIAL1 capture from the ArduPilot bench."""

import os
import sys

os.environ["MAVLINK20"] = "1"
from pymavlink import mavutil


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} mavlink.raw", file=sys.stderr)
        return 2
    link = mavutil.mavlink_connection(
        sys.argv[1], dialect="ardupilotmega", notimestamps=True)
    count = 0
    # ArduPilot writes a short ASCII boot diagnostic before its MAVLink frames.
    # Robust parsing resynchronises after it and reports those bytes as BAD_DATA.
    while True:
        message = link.recv_match(type="HEARTBEAT", blocking=False)
        if message is None:
            break
        if (message.get_srcSystem() == 1
                and message.type == mavutil.mavlink.MAV_TYPE_QUADROTOR
                and message.autopilot == (
                    mavutil.mavlink.MAV_AUTOPILOT_ARDUPILOTMEGA)):
            count += 1
    if count == 0:
        print("FAIL: no ArduCopter HEARTBEAT from system 1", file=sys.stderr)
        return 1
    print(f"PASS: {count} ArduCopter HEARTBEAT messages from system 1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

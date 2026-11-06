#!/usr/bin/env python3
"""Check that an ArduCopter MAVLink file contains a ready EKF report."""
import sys

from pymavlink import mavutil


def main():
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} mavlink.raw")
    needed = (
        mavutil.mavlink.EKF_ATTITUDE
        | mavutil.mavlink.EKF_VELOCITY_HORIZ
        | mavutil.mavlink.EKF_VELOCITY_VERT
        | mavutil.mavlink.EKF_POS_HORIZ_ABS
        | mavutil.mavlink.EKF_POS_VERT_ABS
    )
    constant_position = mavutil.mavlink.EKF_CONST_POS_MODE
    link = mavutil.mavlink_connection(
        sys.argv[1], dialect="ardupilotmega", notimestamps=True
    )
    reports = 0
    ready = None
    while True:
        message = link.recv_match(blocking=False)
        if message is None:
            break
        if message.get_type() == "EKF_STATUS_REPORT":
            reports += 1
            if (message.flags & needed) == needed and not (
                message.flags & constant_position
            ):
                ready = message.flags
    if ready is None:
        raise SystemExit(f"FAIL: no ready EKF_STATUS_REPORT in {reports} reports")
    print(f"PASS: ArduCopter EKF ready flags 0x{ready:04x} in {reports} reports")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

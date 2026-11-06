#!/usr/bin/env python3
"""Make repeated GCS heartbeats and an EKF_STATUS_REPORT stream request."""
import sys

from pymavlink import mavutil


def main():
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} output.raw")
    with open(sys.argv[1], "wb") as output:
        mavlink = mavutil.mavlink.MAVLink(output, srcSystem=255, srcComponent=190)
        for _ in range(400):
            mavlink.heartbeat_send(
                mavutil.mavlink.MAV_TYPE_GCS,
                mavutil.mavlink.MAV_AUTOPILOT_INVALID,
                0,
                0,
                mavutil.mavlink.MAV_STATE_ACTIVE,
            )
            mavlink.request_data_stream_send(
                1, 1, mavutil.mavlink.MAV_DATA_STREAM_EXTENDED_STATUS, 2, 1
            )
            mavlink.command_long_send(
                1,
                1,
                mavutil.mavlink.MAV_CMD_SET_MESSAGE_INTERVAL,
                0,
                193,  # EKF_STATUS_REPORT
                500000,
                0,
                0,
                0,
                0,
                0,
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

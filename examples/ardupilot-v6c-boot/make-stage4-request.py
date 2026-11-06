#!/usr/bin/env python3
"""Make the repeated MAVLink input for the v6C persistence and motor gate."""

import argparse

from pymavlink import mavutil


def heartbeat(mavlink):
    mavlink.heartbeat_send(
        mavutil.mavlink.MAV_TYPE_GCS,
        mavutil.mavlink.MAV_AUTOPILOT_INVALID,
        0,
        0,
        mavutil.mavlink.MAV_STATE_ACTIVE,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output")
    parser.add_argument("mode", choices=("write", "bench"))
    args = parser.parse_args()
    with open(args.output, "wb") as output:
        mavlink = mavutil.mavlink.MAVLink(output, srcSystem=255, srcComponent=190)
        first_boot_params = (
            (b"WP_SPD", 7.25),
            (b"FRAME_CLASS", 1.0),
            (b"FRAME_TYPE", 1.0),
            (b"SERVO1_FUNCTION", 33.0),
            (b"SERVO2_FUNCTION", 34.0),
            (b"SERVO3_FUNCTION", 35.0),
            (b"SERVO4_FUNCTION", 36.0),
            (b"ARMING_SKIPCHK", -1.0),
            (b"BRD_SAFETY_DEFLT", 0.0),
        )
        for iteration in range(240):
            heartbeat(mavlink)
            if args.mode == "write" and iteration % 20 == 19:
                for name, value in first_boot_params:
                    mavlink.param_set_send(1, 1, name, value,
                                           mavutil.mavlink.MAV_PARAM_TYPE_REAL32)
            if args.mode == "bench":
                if iteration in (10, 30, 50):
                    mavlink.command_long_send(
                        1, 1, mavutil.mavlink.MAV_CMD_SET_MESSAGE_INTERVAL, 0,
                        mavutil.mavlink.MAVLINK_MSG_ID_RAW_IMU,
                        100_000, 0, 0, 0, 0, 0,
                    )
                if iteration % 8 == 7:
                    mavlink.param_request_read_send(1, 1, b"WP_SPD", -1)
                if iteration in (80, 100, 120, 140):
                    motor = 1 + (iteration - 80) // 20
                    percent = motor * 10
                    mavlink.command_long_send(
                        1, 1, mavutil.mavlink.MAV_CMD_DO_MOTOR_TEST, 0,
                        motor,
                        mavutil.mavlink.MOTOR_TEST_THROTTLE_PERCENT,
                        percent, 4, 1, 0, 0,
                    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

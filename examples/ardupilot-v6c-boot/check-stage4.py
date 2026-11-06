#!/usr/bin/env python3
"""Check the persisted parameter and v6C motor-test acknowledgements."""

import argparse

from pymavlink import mavutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture")
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    link = mavutil.mavlink_connection(args.capture, dialect="ardupilotmega", notimestamps=True)
    speed = None
    accepted = 0
    params = {}
    while True:
        message = link.recv_match(blocking=False)
        if message is None:
            break
        if message.get_type() == "PARAM_VALUE":
            params[message.param_id.rstrip("\x00")] = message.param_value
        elif (message.get_type() == "COMMAND_ACK"
              and message.command == mavutil.mavlink.MAV_CMD_DO_MOTOR_TEST
              and message.result == mavutil.mavlink.MAV_RESULT_ACCEPTED):
            accepted += 1
    speed = params.get("WP_SPD")
    if speed != 7.25:
        raise SystemExit(f"FAIL: persisted WP_SPD={speed!r}")
    if args.write:
        expected = {
            "FRAME_CLASS": 1.0,
            "FRAME_TYPE": 1.0,
            "SERVO1_FUNCTION": 33.0,
            "SERVO2_FUNCTION": 34.0,
            "SERVO3_FUNCTION": 35.0,
            "SERVO4_FUNCTION": 36.0,
            "ARMING_SKIPCHK": -1.0,
            "BRD_SAFETY_DEFLT": 0.0,
        }
        wrong = {name: params.get(name) for name, value in expected.items()
                 if params.get(name) != value}
        if wrong:
            raise SystemExit(f"FAIL: first-boot parameters {wrong}")
        print(f"PASS: wrote normal F-RAM parameters, WP_SPD={speed:.2f}")
        return 0
    if accepted < 4:
        raise SystemExit(f"FAIL: accepted motor tests={accepted}")
    print(f"PASS: WP_SPD={speed:.2f}, accepted motor tests={accepted}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

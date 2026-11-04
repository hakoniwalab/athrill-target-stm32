#!/usr/bin/env python3
"""Make a repeated GCS heartbeat and sensor/status stream request."""

import argparse

from pymavlink import mavutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output")
    parser.add_argument("--parameters", action="store_true",
                        help="also request sensor device-ID parameters")
    args = parser.parse_args()
    with open(args.output, "wb") as output:
        mavlink = mavutil.mavlink.MAVLink(output, srcSystem=255, srcComponent=190)
        for _ in range(200):
            mavlink.heartbeat_send(
                mavutil.mavlink.MAV_TYPE_GCS,
                mavutil.mavlink.MAV_AUTOPILOT_INVALID,
                0, 0, mavutil.mavlink.MAV_STATE_ACTIVE)
            for stream in (mavutil.mavlink.MAV_DATA_STREAM_EXTENDED_STATUS,
                           mavutil.mavlink.MAV_DATA_STREAM_RAW_SENSORS,
                           mavutil.mavlink.MAV_DATA_STREAM_POSITION):
                mavlink.request_data_stream_send(1, 1, stream, 2, 1)
            if args.parameters and _ % 20 == 19:
                for name in ("INS_ACC_ID", "INS_ACC2_ID", "INS_ACC3_ID",
                             "INS_GYR_ID", "INS_GYR2_ID", "INS_GYR3_ID",
                             "COMPASS_DEV_ID", "COMPASS_DEV_ID2", "COMPASS_DEV_ID3",
                             "BARO1_DEVID", "BARO2_DEVID", "BARO3_DEVID",
                             "GPS_TYPE", "GPS_TYPE2"):
                    mavlink.param_request_read_send(1, 1, name.encode("ascii"), -1)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

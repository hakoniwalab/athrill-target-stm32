#!/usr/bin/env python3
"""Real-time factor of ArduPilot under Renode, measured over MAVLink.

Connects to the telemetry port of Tools/renode/run.py (SERIAL1, TCP 5762),
sends a ground-station heartbeat and a request for all data streams at 2 Hz
every 2 s (the same as examples/ardupilot-bench/make-mavlink-request.py does
for Athrill), and compares ArduPilot's time_boot_ms with the wall clock
between two virtual times.

Usage: measure-rtf.py <label> [--start 20] [--end 80] [--port 5762]
Prints one line: <label> virtual <t0>-><t1> s in <wall> s wall: RTF <rtf>
"""
import argparse
import sys
import time

from pymavlink import mavutil


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("label")
    parser.add_argument("--start", type=float, default=20.0)
    parser.add_argument("--end", type=float, default=80.0)
    parser.add_argument("--port", type=int, default=5762)
    parser.add_argument("--timeout", type=float, default=1500.0, help="wall seconds")
    args = parser.parse_args()

    while True:
        try:
            link = mavutil.mavlink_connection("tcp:localhost:%d" % args.port, source_system=255)
            break
        except OSError:
            time.sleep(0.5)

    begin = time.time()
    last_request = 0.0
    first = None
    boot = None
    while time.time() - begin < args.timeout:
        if time.time() - last_request > 2.0:
            last_request = time.time()
            link.mav.heartbeat_send(mavutil.mavlink.MAV_TYPE_GCS, mavutil.mavlink.MAV_AUTOPILOT_INVALID, 0, 0, 0)
            link.mav.request_data_stream_send(1, 1, mavutil.mavlink.MAV_DATA_STREAM_ALL, 2, 1)
        message = link.recv_match(blocking=True, timeout=1)
        boot_ms = getattr(message, "time_boot_ms", None) if message is not None else None
        if boot_ms is None:
            continue
        boot = boot_ms / 1000.0
        if first is None and boot >= args.start:
            first = (time.time(), boot)
        if first is not None and boot >= args.end:
            wall = time.time() - first[0]
            print("%s virtual %.2f->%.2f s in %.2f s wall: RTF %.3f"
                  % (args.label, first[1], boot, wall, (boot - first[1]) / wall))
            return 0
    print("%s FAILED (last time_boot %s s)" % (args.label, boot))
    return 1


if __name__ == "__main__":
    sys.exit(main())

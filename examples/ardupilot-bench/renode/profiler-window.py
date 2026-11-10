#!/usr/bin/env python3
"""Turn on Renode's machine profiler for a window of ArduPilot's virtual time.

Connects to the telemetry port of Tools/renode/run.py (SERIAL1, TCP 5762), sends
the same ground-station requests as measure-rtf.py, writes
`machine EnableProfiler @<profile>` to Renode's monitor input when time_boot_ms
reaches <start>, and returns when it reaches <end>.

Usage: profiler-window.py <monitor-fifo> <profile> [--start 20] [--end 22]
"""
import argparse
import sys
import time

from pymavlink import mavutil


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("fifo")
    parser.add_argument("profile")
    parser.add_argument("--start", type=float, default=20.0)
    parser.add_argument("--end", type=float, default=22.0)
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
    on = False
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
        if not on and boot >= args.start:
            with open(args.fifo, "w") as monitor:
                monitor.write("machine EnableProfiler @%s\n" % args.profile)
            on = True
            print("profiler on at virtual %.2f s" % boot)
        if on and boot >= args.end:
            print("window done at virtual %.2f s" % boot)
            return 0
    print("FAILED (last time_boot %s)" % boot)
    return 1


if __name__ == "__main__":
    sys.exit(main())

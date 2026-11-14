#!/usr/bin/env python3
"""Waits for ArduCopter's EKF, takes off to 2.5 m in GUIDED and checks the hover (docs/hakoniwa.md, "ArduPilot").

    fly.py [tcp:127.0.0.1:5760]

Waits until EKF_STATUS_REPORT has the attitude, horizontal and vertical velocity and absolute horizontal and
vertical position (not in constant-position mode), then sets GUIDED, arms and sends MAV_CMD_NAV_TAKEOFF (ArduCopter
takes the altitude relative to home). PASS if the vehicle stays within 2.5 m +-0.3 m for the last 10 s of a 40 s
hold. The hold is counted in the vehicle's time (time since boot), and its real-time factor is printed. Needs
pymavlink.
"""
import os
import sys
import time

from pymavlink import mavutil
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from mavlink_watch import PositionWatch  # noqa: E402

TARGET = 2.5
BAND = 0.3
HOLD_SEC = 40.0
TAIL_SEC = 10.0
SYSTEM = 1
COMPONENT = 1
EKF_NEEDED = (mavutil.mavlink.EKF_ATTITUDE | mavutil.mavlink.EKF_VELOCITY_HORIZ | mavutil.mavlink.EKF_VELOCITY_VERT
              | mavutil.mavlink.EKF_POS_HORIZ_ABS | mavutil.mavlink.EKF_POS_VERT_ABS)
EKF_CONST_POS_MODE = mavutil.mavlink.EKF_CONST_POS_MODE
EKF_STATUS_REPORT_ID = 193


class Link:
    def __init__(self, url):
        self.m = mavutil.mavlink_connection(url, source_system=255)
        self.m.wait_heartbeat(timeout=300)
        self.last_heartbeat = 0.0
        self.last_request = 0.0

    def keep_alive(self):
        """GCS heartbeat every second; the telemetry streams requested again every 10 s."""
        now = time.time()
        if now - self.last_heartbeat >= 1.0:
            self.m.mav.heartbeat_send(mavutil.mavlink.MAV_TYPE_GCS, mavutil.mavlink.MAV_AUTOPILOT_INVALID, 0, 0, 0)
            self.last_heartbeat = now
        if now - self.last_request >= 10.0:
            for stream in (mavutil.mavlink.MAV_DATA_STREAM_RAW_SENSORS, mavutil.mavlink.MAV_DATA_STREAM_EXTENDED_STATUS,
                           mavutil.mavlink.MAV_DATA_STREAM_POSITION):
                self.m.mav.request_data_stream_send(SYSTEM, COMPONENT, stream, 4, 1)
            self.m.mav.command_long_send(SYSTEM, COMPONENT, mavutil.mavlink.MAV_CMD_SET_MESSAGE_INTERVAL, 0,
                                         EKF_STATUS_REPORT_ID, 500000, 0, 0, 0, 0, 0)
            self.last_request = now

    def recv(self, types):
        self.keep_alive()
        r = self.m.recv_match(type=types + ["STATUSTEXT"], blocking=True, timeout=1)
        if r is not None and r.get_type() == "STATUSTEXT":
            print("ArduPilot:", r.text, flush=True)
            return None
        return r


def wait_ekf(link, timeout_sec=600):
    t0 = time.time()
    while time.time() - t0 < timeout_sec:
        r = link.recv(["EKF_STATUS_REPORT"])
        if r is None:
            continue
        if (r.flags & EKF_NEEDED) == EKF_NEEDED and not (r.flags & EKF_CONST_POS_MODE):
            print(f"EKF ready (flags 0x{r.flags:04x})", flush=True)
            return
    sys.exit("FAIL: EKF not ready")


def arm(link):
    link.m.set_mode("GUIDED")
    for _ in range(20):
        link.keep_alive()
        link.m.mav.command_long_send(SYSTEM, COMPONENT, mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 0,
                                     1, 0, 0, 0, 0, 0, 0)
        t0 = time.time()
        while time.time() - t0 < 3:
            a = link.recv(["COMMAND_ACK"])
            if a is not None and a.command == mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM:
                if a.result == mavutil.mavlink.MAV_RESULT_ACCEPTED:
                    print("armed", flush=True)
                    return
                break
        time.sleep(2)
    sys.exit("FAIL: arming denied")


def hover(link):
    link.m.mav.command_long_send(SYSTEM, COMPONENT, mavutil.mavlink.MAV_CMD_NAV_TAKEOFF, 0, 0, 0, 0, 0, 0, 0, TARGET)
    wall0 = time.time()
    boot0 = None
    hold_wall0 = None
    tail = []
    zmax = 0.0
    watch = PositionWatch()
    while time.time() - wall0 < 600:
        r = link.recv(["GLOBAL_POSITION_INT"])
        if r is None:
            watch.idle()
            continue
        watch.seen(r.time_boot_ms)
        if boot0 is None:
            boot0 = r.time_boot_ms
            hold_wall0 = time.time()
        held = (r.time_boot_ms - boot0) / 1000.0
        if held >= HOLD_SEC:
            print(f"RTF {held / (time.time() - hold_wall0):.2f} over the hold", flush=True)
            break
        z = r.relative_alt / 1000.0
        zmax = max(zmax, z)
        if held >= HOLD_SEC - TAIL_SEC:
            tail.append(z)
    if not tail:
        sys.exit("FAIL: no position")
    lo = min(tail)
    hi = max(tail)
    print(f"max {zmax:.2f} m, last {TAIL_SEC:.0f} s {lo:.2f} to {hi:.2f} m (target {TARGET} m)", flush=True)
    if abs(lo - TARGET) <= BAND and abs(hi - TARGET) <= BAND:
        print("PASS")
    else:
        sys.exit("FAIL: not hovering at the target")


def main():
    link = Link(sys.argv[1] if len(sys.argv) > 1 else "tcp:127.0.0.1:5760")
    print("heartbeat", flush=True)
    wait_ekf(link)
    arm(link)
    hover(link)


if __name__ == "__main__":
    main()

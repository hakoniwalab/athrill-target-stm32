#!/usr/bin/env python3
"""Takes off to 2.5 m in GUIDED and checks that the x500 comes down when a motor stops (docs/dshot.md, "ArduPilot").

    fly-motor-fault.py [tcp:127.0.0.1:5760]

Run it against examples/ardupilot-hakoniwa-dshot/run.sh started with a motor stop in its extra configuration,
e.g. DEVICE_CONFIG_ESC_MOTOR_STOP 2@70000 (motor 3 from 70 s of vehicle time). Waits for the EKF as fly.py of
examples/ardupilot-hakoniwa does, arms and takes off. PASS if the vehicle climbs above 2 m and is back on the
ground by 120 s of vehicle time. ArduCopter has no motor-failure check on the ESC telemetry: its messages are
printed as they come. Needs pymavlink.
"""
import os
import sys
import time

from pymavlink import mavutil
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from mavlink_watch import PositionWatch  # noqa: E402

TARGET = 2.5
CLIMBED = 2.0     # m: above this the vehicle has taken off
GROUND = 0.3      # m: below this, after climbing, it is down
END_SEC = 120.0   # vehicle time at which the check gives up
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


def fly(link):
    link.m.mav.command_long_send(SYSTEM, COMPONENT, mavutil.mavlink.MAV_CMD_NAV_TAKEOFF, 0, 0, 0, 0, 0, 0, 0, TARGET)
    wall0 = time.time()
    zmax = 0.0
    ground = None
    t = 0.0
    watch = PositionWatch()
    while time.time() - wall0 < 900 and t < END_SEC:
        r = link.recv(["GLOBAL_POSITION_INT"])
        if r is None:
            watch.idle()
            continue
        watch.seen(r.time_boot_ms)
        t = r.time_boot_ms / 1000.0
        z = r.relative_alt / 1000.0
        zmax = max(zmax, z)
        if zmax > CLIMBED and z < GROUND:
            ground = t
            print(f"t={t:7.2f} on the ground (altitude {z:.2f} m)", flush=True)
            break
    print(f"max altitude {zmax:.2f} m, on the ground at {ground}", flush=True)
    if zmax <= CLIMBED:
        sys.exit("FAIL: did not take off")
    if ground is None:
        sys.exit("FAIL: the vehicle did not come down")
    print("PASS: the vehicle came down after the motor stop", flush=True)


def main():
    link = Link(sys.argv[1] if len(sys.argv) > 1 else "tcp:127.0.0.1:5760")
    print("heartbeat", flush=True)
    wait_ekf(link)
    arm(link)
    fly(link)


if __name__ == "__main__":
    main()

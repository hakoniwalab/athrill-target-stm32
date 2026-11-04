#!/usr/bin/env python3
"""Flies PX4 (SIH on the emulated Pixhawk 4) through a scenario over MAVLink,
the way QGroundControl does: arm, takeoff, reposition 5 m north ("Go to
location"), hover, land, and waits for the disarm.

    fly_scenario.py [tcp:127.0.0.1:5760] [--north 5]

The phases are timed by the vehicle's own clock (time_boot_ms).
"""
import argparse
import math
import sys
import time

from pymavlink import mavutil


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("url", nargs="?", default="tcp:127.0.0.1:5760")
    ap.add_argument("--north", type=float, default=5.0, help="reposition north of the takeoff point (m)")
    ap.add_argument("--hover", type=float, default=15.0, help="seconds of hover at the target (vehicle time)")
    args = ap.parse_args()

    m = mavutil.mavlink_connection(args.url, source_system=255, source_component=190)
    m.wait_heartbeat(timeout=180)
    print(f"connected to system {m.target_system}", flush=True)
    state = {"pos": None, "armed": False, "landed": None, "boot": 0}

    def pump(timeout=0.5):
        msg = m.recv_match(blocking=True, timeout=timeout)
        if msg is None:
            return
        t = msg.get_type()
        if t == "GLOBAL_POSITION_INT":
            state["pos"] = msg
            state["boot"] = msg.time_boot_ms / 1000.0
        elif t == "HEARTBEAT" and msg.get_srcSystem() == m.target_system and msg.get_srcComponent() == 1:
            state["armed"] = bool(msg.base_mode & mavutil.mavlink.MAV_MODE_FLAG_SAFETY_ARMED)
        elif t == "EXTENDED_SYS_STATE":
            state["landed"] = msg.landed_state
        elif t == "STATUSTEXT":
            print(f"  [{state['boot']:7.1f}] {msg.text}", flush=True)
        elif t == "COMMAND_ACK":
            print(f"  [{state['boot']:7.1f}] ack {msg.command} result {msg.result}", flush=True)

    def wait(cond, vehicle_seconds, what):
        start = state["boot"]
        deadline = time.time() + 600
        while not cond():
            pump()
            if state["boot"] - start > vehicle_seconds or time.time() > deadline:
                print(f"timeout waiting for {what}", flush=True)
                sys.exit(1)

    def command(cmd, *params):
        p = list(params) + [0.0] * (7 - len(params))
        m.mav.command_long_send(m.target_system, m.target_component, cmd, 0, *p)

    # the estimator settles while the vehicle stands
    wait(lambda: state["pos"] is not None and state["boot"] > 15.0, 120, "15 s of vehicle time")
    home = state["pos"]
    print(f"[{state['boot']:7.1f}] arm and take off", flush=True)
    command(mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 1)
    wait(lambda: state["armed"], 10, "arming")
    command(mavutil.mavlink.MAV_CMD_NAV_TAKEOFF, 0, 0, 0, float("nan"), float("nan"), float("nan"), float("nan"))
    wait(lambda: state["pos"].relative_alt > 2400, 30, "takeoff altitude")
    t = state["boot"]
    wait(lambda: state["boot"] > t + 5.0, 10, "5 s at the takeoff altitude")

    lat = home.lat / 1e7 + args.north / 6371000.0 * 180.0 / math.pi
    lon = home.lon / 1e7
    alt = state["pos"].alt / 1000.0
    print(f"[{state['boot']:7.1f}] reposition {args.north} m north", flush=True)
    command(mavutil.mavlink.MAV_CMD_DO_REPOSITION, -1, mavutil.mavlink.MAV_DO_REPOSITION_FLAGS_CHANGE_MODE, 0,
            float("nan"), lat, lon, alt)
    t = state["boot"]
    wait(lambda: state["boot"] > t + 10.0 + args.hover, 10.0 + args.hover + 5, "the hover at the target")

    print(f"[{state['boot']:7.1f}] land", flush=True)
    command(mavutil.mavlink.MAV_CMD_NAV_LAND, 0, 0, 0, float("nan"), float("nan"), float("nan"), float("nan"))
    wait(lambda: not state["armed"], 60, "landing and disarm")
    print(f"[{state['boot']:7.1f}] disarmed", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())

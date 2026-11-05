#!/usr/bin/env python3
"""Arms PX4 over MAVLink, takes off to 2.5 m and checks the hover (docs/hakoniwa.md).

    fly.py [tcp:127.0.0.1:5760]

The altitude of MAV_CMD_NAV_TAKEOFF is above mean sea level: the home altitude
plus the height to climb. PASS if the vehicle reaches 2.5 m (+-0.3 m) and stays
within that band for the last 10 s of a 40 s hold. Needs pymavlink.
"""
import sys
import threading
import time

from pymavlink import mavutil

TARGET = 2.5
BAND = 0.3


def main():
    url = sys.argv[1] if len(sys.argv) > 1 else "tcp:127.0.0.1:5760"
    m = mavutil.mavlink_connection(url, source_system=255)
    m.wait_heartbeat(timeout=120)
    print("heartbeat", flush=True)
    stop = threading.Event()

    def gcs_heartbeat():
        while not stop.is_set():
            m.mav.heartbeat_send(mavutil.mavlink.MAV_TYPE_GCS, mavutil.mavlink.MAV_AUTOPILOT_INVALID, 0, 0, 0)
            time.sleep(1)

    threading.Thread(target=gcs_heartbeat, daemon=True).start()
    m.mav.request_data_stream_send(m.target_system, m.target_component, mavutil.mavlink.MAV_DATA_STREAM_ALL, 4, 1)

    # wait until PX4 is ready to arm (EKF and sensors settled)
    home = None
    t0 = time.time()
    while time.time() - t0 < 180:
        r = m.recv_match(type=["HOME_POSITION", "STATUSTEXT"], blocking=True, timeout=1)
        if r is None:
            m.mav.command_long_send(m.target_system, m.target_component,
                                    mavutil.mavlink.MAV_CMD_REQUEST_MESSAGE, 0, 242, 0, 0, 0, 0, 0, 0)
            continue
        if r.get_type() == "STATUSTEXT":
            print("PX4:", r.text, flush=True)
        else:
            home = r.altitude / 1000.0
            break
    if home is None:
        sys.exit("FAIL: no home position")
    print(f"home altitude {home:.2f} m (AMSL)", flush=True)

    for _ in range(30):
        m.mav.command_long_send(m.target_system, m.target_component,
                                mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 0, 1, 0, 0, 0, 0, 0, 0)
        a = m.recv_match(type="COMMAND_ACK", blocking=True, timeout=5)
        if a and a.command == mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM and a.result == 0:
            break
        time.sleep(2)
    else:
        sys.exit("FAIL: arming denied")
    print("armed", flush=True)

    m.mav.command_long_send(m.target_system, m.target_component, mavutil.mavlink.MAV_CMD_NAV_TAKEOFF,
                            0, 0, 0, 0, float("nan"), float("nan"), float("nan"), home + TARGET)
    t0 = time.time()
    tail = []
    zmax = 0.0
    while time.time() - t0 < 40:
        r = m.recv_match(type=["GLOBAL_POSITION_INT", "STATUSTEXT"], blocking=True, timeout=1)
        if r is None:
            continue
        if r.get_type() == "STATUSTEXT":
            print("PX4:", r.text, flush=True)
            continue
        z = r.relative_alt / 1000.0
        zmax = max(zmax, z)
        if time.time() - t0 > 30:
            tail.append(z)
    stop.set()
    if not tail:
        sys.exit("FAIL: no position")
    lo, hi = min(tail), max(tail)
    print(f"max {zmax:.2f} m, last 10 s {lo:.2f} to {hi:.2f} m (target {TARGET} m)", flush=True)
    if abs(lo - TARGET) <= BAND and abs(hi - TARGET) <= BAND:
        print("PASS")
    else:
        sys.exit("FAIL: not hovering at the target")


if __name__ == "__main__":
    main()

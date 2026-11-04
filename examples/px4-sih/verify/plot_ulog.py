#!/usr/bin/env python3
"""Plots a PX4 SIH flight log (ULog) of the emulated Pixhawk 4 for
docs/verification.md: altitude, top view of the path, tilt, EKF2 test ratios.

    plot_ulog.py <log.ulg> <output directory> [--target-north 5 --target-east 0]

Each chart is written twice, <name>-light.png and <name>-dark.png, for
GitHub's light and dark themes. Needs numpy, matplotlib and pyulog.
"""
import argparse
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from pyulog import ULog  # noqa: E402

# The reference palette of the categorical slots 1-4 (validated for both modes),
# with the surfaces and text inks of each mode.
THEMES = {
    "light": {"surface": "#fcfcfb", "text": "#0b0b0b", "muted": "#52514e", "grid": "#e4e3df",
              "series": ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]},
    "dark": {"surface": "#1a1a19", "text": "#f0efec", "muted": "#c3c2b7", "grid": "#3a3936",
             "series": ["#3987e5", "#d95926", "#199e70", "#c98500"]},
}


def topic(ulog, name):
    for d in ulog.data_list:
        if d.name == name and d.multi_id == 0:
            return d.data
    return None


def tilt_deg(data, prefix):
    x = data[f"{prefix}[1]"]
    y = data[f"{prefix}[2]"]
    return np.degrees(np.arccos(np.clip(1.0 - 2.0 * (x * x + y * y), -1.0, 1.0)))


def style(ax, fig, theme, title, xlabel, ylabel):
    fig.patch.set_facecolor(theme["surface"])
    ax.set_facecolor(theme["surface"])
    ax.set_title(title, color=theme["text"], loc="left", fontsize=12, fontweight="bold", pad=10)
    ax.set_xlabel(xlabel, color=theme["muted"], fontsize=10)
    ax.set_ylabel(ylabel, color=theme["muted"], fontsize=10)
    ax.tick_params(colors=theme["muted"], labelsize=9, length=0)
    ax.grid(True, color=theme["grid"], linewidth=0.8)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(theme["grid"])


def end_label(ax, x, y, text, theme, dy=0.0):
    """A direct label at the end of a line, in text ink (the line carries the colour)."""
    ax.annotate(text, (x, y), xytext=(6, dy), textcoords="offset points", color=theme["text"],
                fontsize=9, va="center")


def legend(ax, theme):
    leg = ax.legend(frameon=False, fontsize=9, loc="upper left", ncol=4, bbox_to_anchor=(0.0, -0.14))
    for t in leg.get_texts():
        t.set_color(theme["text"])


def save(fig, out, name, mode):
    fig.savefig(os.path.join(out, f"{name}-{mode}.png"), dpi=150, bbox_inches="tight",
                facecolor=fig.get_facecolor())
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ulog")
    ap.add_argument("out")
    ap.add_argument("--target-north", type=float, default=None)
    ap.add_argument("--target-east", type=float, default=None)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    u = ULog(args.ulog)
    t0 = u.start_timestamp

    def secs(d):
        return (d["timestamp"].astype(np.int64) - int(t0)) / 1e6

    lpos = topic(u, "vehicle_local_position")
    truth = topic(u, "vehicle_local_position_groundtruth")
    sp = topic(u, "vehicle_local_position_setpoint")
    att = topic(u, "vehicle_attitude")
    att_sp = topic(u, "vehicle_attitude_setpoint")
    est = topic(u, "estimator_status")
    land = topic(u, "vehicle_land_detected")

    landed = land["landed"].astype(bool)
    t_land = secs(land)
    t_takeoff = t_land[~landed][0]
    t_touchdown = t_land[(t_land > t_land[~landed][-1]) & landed][0]

    for mode, theme in THEMES.items():
        s = theme["series"]

        # 1. altitude: SIH ground truth, EKF2 estimate, setpoint
        fig, ax = plt.subplots(figsize=(8, 3.6))
        style(ax, fig, theme, "Altitude above home: EKF2 estimate on SIH ground truth",
              "time since arming (s)", "altitude (m)")
        ok = np.isfinite(sp["z"])
        ax.plot(secs(sp)[ok], -sp["z"][ok], color=theme["muted"], linewidth=1.5, linestyle=(0, (4, 3)),
                label="setpoint")
        ax.plot(secs(truth), -truth["z"], color=s[0], linewidth=2.0, label="SIH ground truth")
        ax.plot(secs(lpos), -lpos["z"], color=s[1], linewidth=2.0, linestyle=(0, (1, 1.5)),
                label="EKF2 estimate")
        for t, text in ((t_takeoff, "takeoff"), (t_touchdown, "touchdown")):
            ax.axvline(t, color=theme["grid"], linewidth=1.0, zorder=0)
            ax.annotate(text, (t, ax.get_ylim()[1]), xytext=(3, -12), textcoords="offset points",
                        color=theme["muted"], fontsize=8)
        legend(ax, theme)
        save(fig, args.out, "altitude", mode)

        # 2. top view of the path
        fig, ax = plt.subplots(figsize=(5.2, 5.2))
        style(ax, fig, theme, "Path from above: go to 5 m north", "east (m)", "north (m)")
        flying = (secs(truth) > t_takeoff) & (secs(truth) < t_touchdown)
        ax.plot(truth["y"][flying], truth["x"][flying], color=s[0], linewidth=2.0, label="SIH ground truth")
        flying = (secs(lpos) > t_takeoff) & (secs(lpos) < t_touchdown)
        ax.plot(lpos["y"][flying], lpos["x"][flying], color=s[1], linewidth=2.0, linestyle=(0, (1, 1.5)),
                label="EKF2 estimate")
        ax.plot([0.0], [0.0], marker="o", markersize=8, color=theme["muted"], linestyle="none",
                markeredgecolor=theme["surface"], markeredgewidth=2)
        end_label(ax, 0.0, 0.0, "start", theme)
        if args.target_north is not None:
            ax.plot([args.target_east], [args.target_north], marker="x", markersize=10, markeredgewidth=2,
                    color=theme["text"], linestyle="none", label="target")
            end_label(ax, args.target_east, args.target_north, "target", theme)
        ax.set_aspect("equal")
        lim = max(abs(args.target_north or 1.0), 1.0) + 1.0
        ax.set_xlim(-lim / 2, lim / 2)
        ax.set_ylim(-1.0, lim)
        legend(ax, theme)
        save(fig, args.out, "path", mode)

        # 3. tilt: setpoint and estimate
        fig, ax = plt.subplots(figsize=(8, 3.2))
        style(ax, fig, theme, "Tilt: attitude setpoint and the vehicle", "time since arming (s)", "tilt (deg)")
        ax.plot(secs(att_sp), tilt_deg(att_sp, "q_d"), color=theme["muted"], linewidth=1.5,
                linestyle=(0, (4, 3)), label="setpoint")
        ax.plot(secs(att), tilt_deg(att, "q"), color=s[0], linewidth=2.0, label="vehicle (EKF2)")
        ax.set_xlim(t_takeoff - 2, t_touchdown + 2)
        legend(ax, theme)
        save(fig, args.out, "tilt", mode)

        # 4. EKF2 innovation test ratios against the gate
        fig, ax = plt.subplots(figsize=(8, 3.2))
        style(ax, fig, theme, "EKF2 innovation test ratios (fused when below 1)",
              "time since arming (s)", "test ratio")
        names = (("pos", "position"), ("vel", "velocity"), ("hgt", "height"), ("hdg", "heading"))
        for k, (key, name) in enumerate(names):
            v = est[f"{key}_test_ratio"]
            ax.plot(secs(est), v, color=s[k], linewidth=1.5, label=name)
            ok = np.isfinite(v)
            end_label(ax, secs(est)[ok][-1], v[ok][-1], name, theme, dy=(k - 1.5) * 9)
        ax.axhline(1.0, color=theme["muted"], linewidth=1.5, linestyle=(0, (4, 3)))
        ax.annotate("gate", (secs(est)[0], 1.0), xytext=(0, 4), textcoords="offset points",
                    color=theme["muted"], fontsize=8)
        ax.set_ylim(0, 1.1)
        ax.set_xlim(secs(est)[0], secs(est)[-1] + (secs(est)[-1] - secs(est)[0]) * 0.12)
        legend(ax, theme)
        save(fig, args.out, "ekf2-ratios", mode)
    print(f"charts in {args.out}")


if __name__ == "__main__":
    main()

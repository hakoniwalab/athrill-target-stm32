#!/usr/bin/env python3
"""Compare C/DT in-process flight values at each 4 ms aircraft sample."""

import re
import struct
import sys
from pathlib import Path

ACTUATOR_LIMIT = 0.0021  # two 1 us PWM quantization steps
ALTITUDE_LIMIT_MM = 2
MIN_CLIMB_MM = 2000


def float32(value):
    return struct.unpack(">f", bytes.fromhex(value))[0]


def read_trace(path):
    actuators = []
    altitudes = []
    for line in path.read_text().splitlines():
        if line.startswith("actuator "):
            match = re.search(r"time=(\d+) controls=([^ ]+)", line)
            if match is None:
                raise ValueError(f"malformed actuator record in {path}: {line}")
            actuators.append((int(match[1]),
                [float32(value) for value in match[2].split(",")]))
        elif line.startswith("gps "):
            match = re.search(r"time=(\d+).* alt=(-?\d+)", line)
            if match is None:
                raise ValueError(f"malformed GPS record in {path}: {line}")
            altitudes.append((int(match[1]), int(match[2])))
    return actuators, altitudes


def same_times(left, right, kind):
    if len(left) != len(right):
        raise ValueError(f"{kind} record count differs: {len(left)} != {len(right)}")
    for index, (a, b) in enumerate(zip(left, right)):
        if a[0] != b[0]:
            raise ValueError(f"{kind} time differs at record {index}: {a[0]} != {b[0]}")


def main():
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} C_TRACE DT_TRACE")
    c_actuators, c_altitudes = read_trace(Path(sys.argv[1]))
    dt_actuators, dt_altitudes = read_trace(Path(sys.argv[2]))
    same_times(c_actuators, dt_actuators, "actuator")
    same_times(c_altitudes, dt_altitudes, "GPS")
    if not c_actuators or not c_altitudes:
        raise ValueError("trace has no actuator or GPS records")

    actuator_max = (0.0, 0, 0, 0.0, 0.0)
    first_actuator_difference = None
    for (time, c_values), (_, dt_values) in zip(c_actuators, dt_actuators):
        if len(c_values) != len(dt_values):
            raise ValueError(f"actuator count differs at {time}")
        for channel, (c_value, dt_value) in enumerate(zip(c_values, dt_values), 1):
            delta = abs(c_value - dt_value)
            if delta > actuator_max[0]:
                actuator_max = (delta, time, channel, c_value, dt_value)
            if delta != 0.0 and first_actuator_difference is None:
                first_actuator_difference = (time, channel, c_value, dt_value)

    altitude_max = max(
        (abs(c_alt - dt_alt), time, c_alt, dt_alt)
        for (time, c_alt), (_, dt_alt) in zip(c_altitudes, dt_altitudes))
    c_climb = max(value for _, value in c_altitudes) - c_altitudes[0][1]
    dt_climb = max(value for _, value in dt_altitudes) - dt_altitudes[0][1]
    if actuator_max[0] > ACTUATOR_LIMIT:
        raise ValueError(f"actuator delta exceeds limit: {actuator_max}")
    if altitude_max[0] > ALTITUDE_LIMIT_MM:
        raise ValueError(f"altitude delta exceeds limit: {altitude_max}")
    if min(c_climb, dt_climb) < MIN_CLIMB_MM:
        raise ValueError(f"takeoff not observed: climb C={c_climb} DT={dt_climb} mm")

    print(f"actuator records={len(c_actuators)} max_delta={actuator_max[0]:.9f} "
          f"time={actuator_max[1]} channel={actuator_max[2]}")
    if first_actuator_difference is not None:
        print("first actuator difference: "
              f"time={first_actuator_difference[0]} channel={first_actuator_difference[1]} "
              f"C={first_actuator_difference[2]:.9f} DT={first_actuator_difference[3]:.9f}")
    print(f"GPS records={len(c_altitudes)} max_altitude_delta={altitude_max[0]}mm "
          f"time={altitude_max[1]} climb_C={c_climb}mm climb_DT={dt_climb}mm")
    print("PASS: armed C/DT actuator and altitude traces agree within PWM/GPS resolution")


if __name__ == "__main__":
    try:
        main()
    except ValueError as error:
        raise SystemExit(f"FAIL: {error}")

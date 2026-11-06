#!/usr/bin/env python3
"""Validate and summarize the PX4 v6C sensor listener capture."""

import math
import re
import sys


def topic_section(text: str, topic: str) -> str:
    marker = f"TOPIC: {topic}"
    start = text.rfind(marker)
    if start < 0:
        raise ValueError(f"missing {marker}")
    end = text.find("nsh>", start)
    return text[start:end if end >= 0 else None]


def records(section: str, topic: str) -> list[dict[str, float]]:
    chunks = re.findall(
        rf"\n {re.escape(topic)}\n(.*?)(?=\n(?:Instance \d+:|nsh>|\[K)|\Z)",
        section,
        re.DOTALL,
    )
    result = []
    for chunk in chunks:
        values = {}
        for field in ("x", "y", "z", "pressure", "temperature"):
            match = re.search(rf"^\s*{field}:\s*(-?\d+(?:\.\d+)?)", chunk, re.MULTILINE)
            if match:
                values[field] = float(match.group(1))
        result.append(values)
    return result


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} console.raw", file=sys.stderr)
        return 2
    text = open(sys.argv[1], "rb").read().decode("utf-8", "ignore").replace("\r", "")
    if "selected gyro:" not in text or "selected accel:" not in text:
        raise SystemExit("FAIL: sensors status did not select accel and gyro")

    accel = records(topic_section(text, "sensor_accel"), "sensor_accel")
    gyro = records(topic_section(text, "sensor_gyro"), "sensor_gyro")
    baro = records(topic_section(text, "sensor_baro"), "sensor_baro")
    mag = records(topic_section(text, "sensor_mag"), "sensor_mag")
    if len(accel) != 2 or len(gyro) != 2 or len(baro) != 1 or len(mag) != 1:
        raise SystemExit(
            f"FAIL: instances accel={len(accel)} gyro={len(gyro)} baro={len(baro)} mag={len(mag)}"
        )
    for index, sample in enumerate(accel):
        norm = math.sqrt(sum(sample[axis] ** 2 for axis in "xyz"))
        if not 9.0 <= norm <= 10.6:
            raise SystemExit(f"FAIL: accel {index} norm {norm}")
    for index, sample in enumerate(gyro):
        norm = math.sqrt(sum(sample[axis] ** 2 for axis in "xyz"))
        if norm > 0.1:
            raise SystemExit(f"FAIL: gyro {index} norm {norm}")
    if not 80000.0 <= baro[0]["pressure"] <= 120000.0:
        raise SystemExit(f"FAIL: pressure {baro[0]['pressure']}")
    mag_norm = math.sqrt(sum(mag[0][axis] ** 2 for axis in "xyz"))
    if not 0.2 <= mag_norm <= 1.0:
        raise SystemExit(f"FAIL: magnetic field norm {mag_norm}")

    print("PASS: PX4 v6C live sensors")
    for index, sample in enumerate(accel):
        print(f"accel{index}: {sample['x']:.5f} {sample['y']:.5f} {sample['z']:.5f} m/s^2")
    for index, sample in enumerate(gyro):
        print(f"gyro{index}: {sample['x']:.5f} {sample['y']:.5f} {sample['z']:.5f} rad/s")
    print(f"baro: {baro[0]['pressure']:.5f} Pa {baro[0]['temperature']:.5f} C")
    print(f"mag: {mag[0]['x']:.5f} {mag[0]['y']:.5f} {mag[0]['z']:.5f} gauss")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

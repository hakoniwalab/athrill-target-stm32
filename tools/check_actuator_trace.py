#!/usr/bin/env python3
"""Check ordered output:timer:channel:pulse entries in an actuator trace."""

import re
import sys


def main():
    if len(sys.argv) < 3:
        raise SystemExit(f"usage: {sys.argv[0]} trace output:timer:channel:pulse ...")
    entries = []
    pattern = re.compile(r"output=(\d+) timer=(\d+) channel=(\d+) pulse_us=(\d+)")
    for line in open(sys.argv[1], encoding="ascii"):
        match = pattern.search(line)
        if match:
            entries.append(tuple(map(int, match.groups())))
    position = 0
    for text in sys.argv[2:]:
        expected = tuple(map(int, text.split(":")))
        while position < len(entries) and entries[position] != expected:
            position += 1
        if position == len(entries):
            raise SystemExit(f"FAIL: missing ordered actuator entry {expected}")
        position += 1
    print("PASS: ordered actuator entries " + " ".join(sys.argv[2:]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

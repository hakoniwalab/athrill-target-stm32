#!/usr/bin/env python3
"""Fails on any error or warning in a console or log that the board's allow-list does not explain.

    examples/console-check.py <allow-list> <console or log>...

Every line matching ERROR, WARN, FAIL, failed or one of Athrill's access errors must match an
entry of the allow-list (athrill-device boards/<board>/regression/<firmware>.allow; format and
categories are described at its top). Warnings that mean the emulated system is too slow (the
logger watchdog, missing CPU load information) are never allowed: they showed a model timing gap
(athrill-device#103). Exit status 0 when every line is explained, 1 otherwise, 2 on a bad list.
"""
import re
import sys

FLAGGED = re.compile(r"ERROR|WARN|FAIL|failed|not found error|can not load")
# uORB field dumps (`listener`), e.g. "rtcm_crc_failed: False", are values, not messages
FIELD = re.compile(r"^\s*[a-z0-9_]+(\[\d+\])?:\s")
NEVER = re.compile(r"Watchdog triggers|Writing watchdog data|No CPU and RAM load information")
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


def read_allow(path):
    entries = []
    for number, line in enumerate(open(path, encoding="utf-8"), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(" | ")
        if len(parts) != 3 or not parts[2].strip():
            sys.exit(f"{path}:{number}: want 'category | regex | reason'")
        category, pattern, reason = (p.strip() for p in parts)
        if category.split()[0] not in ("board", "gap", "transient", "firstboot"):
            sys.exit(f"{path}:{number}: unknown category {category!r}")
        entries.append({"category": category, "regex": re.compile(pattern), "reason": reason, "count": 0})
    return entries


def main():
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    entries = read_allow(sys.argv[1])
    unexplained = []
    for path in sys.argv[2:]:
        with open(path, "rb") as f:
            text = ANSI.sub("", f.read().decode("utf-8", "replace"))
        for line in text.splitlines():
            if not FLAGGED.search(line) or FIELD.match(line):
                continue
            if NEVER.search(line):
                unexplained.append((path, line.strip(), "never allowed: the system ran too slowly"))
                continue
            for entry in entries:
                if entry["regex"].search(line):
                    entry["count"] += 1
                    break
            else:
                unexplained.append((path, line.strip(), "not in the allow-list"))
    for entry in entries:
        if entry["count"]:
            print(f"  allowed {entry['count']:4d}x [{entry['category']}] {entry['regex'].pattern}")
    for path, line, why in unexplained:
        print(f"  [FAIL] {path}: {line}  ({why})")
    if unexplained:
        print(f"console check: {len(unexplained)} unexplained line(s)")
        return 1
    print("console check: every error and warning is explained")
    return 0


if __name__ == "__main__":
    sys.exit(main())

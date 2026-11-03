#!/usr/bin/env python3
"""Which instruction encodings with an executor the tests run.

Reads the executor tables of src/cpu/thumb_exec16.c and thumb_exec32.c
(generated id -> executor function) and the function execution counts of a
coverage run (llvm-cov export JSON, from tools/coverage.sh). An encoding is
covered when its executor ran.

Usage:
  python tools/encoding_coverage.py <coverage.json> [--usage <usage.txt>]
--usage takes the output of tools/insn_usage.py and adds, for the encodings
that firmware uses, whether they have an executor and whether it is tested.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def executors():
    """{encoding name: executor function}"""
    table = {}
    for name, width in (("thumb_exec16.c", "16"), ("thumb_exec32.c", "32")):
        text = open(os.path.join(ROOT, "src", "cpu", name)).read()
        for encoding, function in re.findall(r"\[T32_%s_([A-Za-z0-9_]+)\] = (\w+)," % width, text):
            table[encoding] = function
    return table


def function_counts(path):
    """{function name: execution count} (static functions are "file:name")."""
    counts = {}
    for data in json.load(open(path))["data"]:
        for function in data["functions"]:
            counts[function["name"].split(":")[-1]] = function["count"]
    return counts


def main():
    if len(sys.argv) not in (2, 4):
        sys.exit(__doc__)
    table = executors()
    counts = function_counts(sys.argv[1])
    tested = sorted(e for e, f in table.items() if counts.get(f, 0) > 0)
    untested = sorted(e for e, f in table.items() if counts.get(f, 0) == 0)
    print("encodings with an executor: %d, run by the tests: %d (%.1f%%)"
          % (len(table), len(tested), 100.0 * len(tested) / len(table)))
    if untested:
        print("not run by the tests: " + " ".join(untested))
    if len(sys.argv) == 4 and sys.argv[2] == "--usage":
        used = {}
        for line in open(sys.argv[3]):
            m = re.match(r"^(\S+)\s+(\d+)\s+(\d+)\s+(yes|-)\s", line)
            if m:
                used[m.group(1)] = int(m.group(2))
        total = sum(used.values())
        with_exec = {e: n for e, n in used.items() if e in table}
        with_test = {e: n for e, n in with_exec.items() if e in tested}
        print("firmware start-up: %d encodings (%d uses); with an executor %d (%.1f%% of uses), tested %d (%.1f%% of uses)"
              % (len(used), total, len(with_exec), 100.0 * sum(with_exec.values()) / total,
                 len(with_test), 100.0 * sum(with_test.values()) / total))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Compare two PC traces past their first difference.

compare_pc_trace.py stops at the first differing instruction. This script
goes on: at a difference it searches a little ahead in both traces for the
next WINDOW identical PCs, records the divergence, and continues from there.
It reports how many instructions were identical in lock-step and, grouped by
the functions they started in, where the traces diverged.

Usage: resync_pc_trace.py <athrill-trace> <other-trace> <elf> [--limit N]
Each trace holds one hex PC per line (Athrill's and Renode's format).
"""
import argparse
import bisect
import collections
import subprocess
import sys

WINDOW = 64      # identical PCs needed to accept a resynchronisation
SEARCH = 20000   # how far ahead to search, in instructions


def read_trace(path, limit):
    pcs = []
    with open(path) as f:
        for line in f:
            if limit is not None and len(pcs) >= limit:
                break
            line = line.strip()
            if line:
                pcs.append(int(line, 16))
    return pcs


def symbol_table(elf):
    for nm in ("arm-none-eabi-nm", "llvm-nm"):
        try:
            out = subprocess.run([nm, "-n", elf], capture_output=True, text=True, check=True).stdout
        except (OSError, subprocess.CalledProcessError):
            continue
        symbols = []
        for line in out.splitlines():
            fields = line.split()
            if len(fields) == 3 and fields[1] in "tTwW":
                symbols.append((int(fields[0], 16) & ~1, fields[2]))
        return symbols
    sys.exit("arm-none-eabi-nm or llvm-nm is needed")


def resync(a, b, i, j):
    """The next (i', j') after i, j from which WINDOW PCs agree, or None."""
    for distance in range(1, SEARCH):
        for skip_a in range(distance + 1):
            ii = i + skip_a
            jj = j + distance - skip_a
            if ii + WINDOW <= len(a) and jj + WINDOW <= len(b) and a[ii:ii + WINDOW] == b[jj:jj + WINDOW]:
                return ii, jj
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("athrill")
    parser.add_argument("other")
    parser.add_argument("elf")
    parser.add_argument("--limit", type=int, help="read at most this many PCs of each trace")
    args = parser.parse_args()

    a = read_trace(args.athrill, args.limit)
    b = read_trace(args.other, args.limit)
    symbols = symbol_table(args.elf)
    addresses = [s[0] for s in symbols]

    def function(pc):
        k = bisect.bisect_right(addresses, pc) - 1
        return symbols[k][1] if k >= 0 else "?"

    i = j = 0
    identical = 0
    events = []
    while i < len(a) and j < len(b):
        if a[i] == b[j]:
            i += 1
            j += 1
            identical += 1
            continue
        found = resync(a, b, i, j)
        if found is None:
            events.append((i, j, function(a[i]), function(b[j]), "no resynchronisation"))
            break
        events.append((i, j, function(a[i]), function(b[j]),
                       "skipped %d / %d" % (found[0] - i, found[1] - j)))
        i, j = found

    print("compared %d and %d instructions; identical in lock-step: %d" % (i, j, identical))
    print("divergences: %d" % len(events))
    for (fa, fb), n in collections.Counter((e[2], e[3]) for e in events).most_common(12):
        print("  %5d x  %s / %s" % (n, fa, fb))
    print("first divergences:")
    for e in events[:3]:
        print("  index %d / %d in %s / %s, %s" % e)


if __name__ == "__main__":
    main()

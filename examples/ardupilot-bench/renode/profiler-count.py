#!/usr/bin/env python3
"""Count the entries of a Renode machine-profiler file in a window of virtual time.

Prints the executed instructions, RAM accesses, peripheral (MMIO) accesses and
interrupt entries between <from-ms> and <to-ms> of virtual time, and the MMIO
accesses by peripheral. With --pages, also writes the reads and writes per
256-byte page.

Usage: profiler-count.py <profile> [--from-ms 20500] [--to-ms 21500] [--pages FILE]
"""
import argparse
import bisect
import collections
import struct
import sys

ENTRY = struct.Struct("<qdc")             # real time (ticks), virtual time (ms), type
BODY = {0: 12, 1: 1, 2: 9, 3: 8}           # instructions, memory, peripheral, exception


def read(f, fmt):
    return struct.unpack(fmt, f.read(struct.calcsize(fmt)))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("profile")
    parser.add_argument("--from-ms", type=float, default=20500.0)
    parser.add_argument("--to-ms", type=float, default=21500.0)
    parser.add_argument("--pages")
    args = parser.parse_args()

    f = open(args.profile, "rb")
    for _ in range(read(f, "i")[0]):
        read(f, "i")
        read(f, "%ds" % read(f, "i")[0])
    peripherals = []
    for _ in range(read(f, "i")[0]):
        name = read(f, "%ds" % read(f, "i")[0])[0].decode()
        start, end = read(f, "2Q")
        peripherals.append((start, end, name))
    peripherals.sort()
    starts = [p[0] for p in peripherals]

    counts = collections.Counter()
    by_peripheral = collections.Counter()
    pages = collections.Counter()
    first = last = None
    while True:
        header = f.read(ENTRY.size)
        if len(header) < ENTRY.size:
            break
        _, virtual_ms, kind = ENTRY.unpack(header)
        kind = kind[0]
        body = f.read(BODY[kind])
        if len(body) < BODY[kind]:
            break
        if not args.from_ms <= virtual_ms < args.to_ms:
            continue
        counts[kind] += 1
        if kind == 0:
            executed = struct.unpack("<iQ", body)[1]
            first = executed if first is None else first
            last = executed
        elif kind == 2:
            operation, address = struct.unpack("<cQ", body)
            pages[(address & ~0xff, operation[0])] += 1
            i = bisect.bisect_right(starts, address) - 1
            inside = i >= 0 and address <= peripherals[i][1]
            by_peripheral[peripherals[i][2] if inside else hex(address & ~0x3ff)] += 1

    print("virtual %.0f to %.0f ms" % (args.from_ms, args.to_ms))
    print("instructions %d" % ((last - first) if first is not None else 0))
    print("RAM accesses %d, MMIO accesses %d, interrupt entries %d" % (counts[1], counts[2], counts[3]))
    for name, n in by_peripheral.most_common():
        print("%10d %s" % (n, name))
    if args.pages:
        with open(args.pages, "w") as out:
            for page in sorted({p for p, _ in pages}):
                out.write("0x%08x %d %d\n" % (page, pages[(page, 0)], pages[(page, 1)]))
    return 0


if __name__ == "__main__":
    sys.exit(main())

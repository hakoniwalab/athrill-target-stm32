#!/usr/bin/env python3
"""Inclusive share of one thread's host time, by groups of frames, from a macOS `sample` call graph.

A group is a name and a regular expression on the frame's name. A sample counts for a group when any frame of its
stack matches; nested matches are counted once. Addresses in JIT-compiled code are named through a perf map
(`DOTNET_PerfMapEnabled=1` for .NET); unnamed ones are named "translated or JIT code".

Usage: sample-share.py <sample.txt> <thread-name> [--perfmap FILE] --group NAME=REGEX [--group ...]
"""
import argparse
import bisect
import re
import sys

NODE = re.compile(r"^(\s*[+!:| ]*)(\d+) (.*)$")
THREAD = re.compile(r"^    \d+ Thread_")
UNKNOWN = re.compile(r"\?\?\?  \(in <unknown binary>\)  \[(0x[0-9a-f]+)")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("sample")
    parser.add_argument("thread")
    parser.add_argument("--perfmap")
    parser.add_argument("--group", action="append", default=[], metavar="NAME=REGEX")
    args = parser.parse_args()

    entries = []
    if args.perfmap:
        for line in open(args.perfmap, errors="replace"):
            address, size, name = line.rstrip("\n").split(" ", 2)
            entries.append((int(address, 16), int(size, 16), name))
    entries.sort()
    starts = [e[0] for e in entries]

    def frame(text):
        m = UNKNOWN.match(text)
        if not m:
            return text
        address = int(m.group(1), 16)
        i = bisect.bisect_right(starts, address) - 1
        if i >= 0 and address < entries[i][0] + entries[i][1]:
            return entries[i][2]
        return "translated or JIT code"

    groups = [(g.split("=", 1)[0], re.compile(g.split("=", 1)[1])) for g in args.group]
    lines = open(args.sample, errors="replace").read().split("\n")
    start = next(i for i, l in enumerate(lines) if THREAD.match(l) and args.thread in l)
    total = None
    counts = [0] * len(groups)
    inside = [None] * len(groups)
    for line in lines[start:]:
        if total is not None and (not line.strip() or THREAD.match(line)):
            break
        m = NODE.match(line)
        if not m:
            continue
        depth, n, name = len(m.group(1)), int(m.group(2)), frame(m.group(3))
        if total is None:
            total = n
            continue
        for k, (_, pattern) in enumerate(groups):
            if inside[k] is not None and depth <= inside[k]:
                inside[k] = None
            if inside[k] is None and pattern.search(name):
                counts[k] += n
                inside[k] = depth
    print("%s: %d samples" % (args.thread, total))
    for (name, _), n in zip(groups, counts):
        print("%6.1f%%  %s" % (100.0 * n / total, name))
    return 0


if __name__ == "__main__":
    sys.exit(main())

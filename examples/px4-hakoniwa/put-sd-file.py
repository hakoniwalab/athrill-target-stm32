#!/usr/bin/env python3
"""Puts one file into a blank SD card image made by make-sd-image.sh.

    put-sd-file.py <sd.img> <file> <8.3 NAME>

The image must be the blank one of make-sd-image.sh (FAT32, 1 GB, only the
volume label in the root directory): the file goes into the root directory,
from cluster 3 on, with a fixed date. So the result is the same, byte for
byte, on every host, and no host tool (hdiutil, mtools) is needed.
"""
import sys

SECTOR = 512
RESERVED = 32
FAT_SECTORS = 2044
SECTORS_PER_CLUSTER = 8
CLUSTER = SECTOR * SECTORS_PER_CLUSTER
DATA = RESERVED + 2 * FAT_SECTORS
FIRST = 3                      # the root directory is cluster 2
DATE = ((2026 - 1980) << 9) | (11 << 5) | 4


def short_name(name):
    base, _, ext = name.upper().partition(".")
    if not (1 <= len(base) <= 8 and len(ext) <= 3) or not (base + ext).isalnum():
        sys.exit(f"not an 8.3 name: {name}")
    return (base.ljust(8) + ext.ljust(3)).encode()


def main():
    image, path, name = sys.argv[1:4]
    data = open(path, "rb").read()
    count = max(1, -(-len(data) // CLUSTER))
    with open(image, "r+b") as f:
        f.seek(DATA * SECTOR + 32)
        if f.read(32) != bytes(32):
            sys.exit(f"{image}: the root directory is not blank (only a blank image is supported)")
        # FAT chain, in both copies
        for fat in (RESERVED, RESERVED + FAT_SECTORS):
            for i in range(count):
                cluster = FIRST + i
                nxt = 0x0FFFFFFF if i == count - 1 else cluster + 1
                f.seek(fat * SECTOR + 4 * cluster)
                f.write(nxt.to_bytes(4, "little"))
        # FSInfo (primary and backup): free clusters and the next free one
        for fsinfo in (1, 7):
            f.seek(fsinfo * SECTOR + 488)
            free = int.from_bytes(f.read(4), "little")
            f.seek(fsinfo * SECTOR + 488)
            f.write((free - count).to_bytes(4, "little") + (FIRST + count).to_bytes(4, "little"))
        # the directory entry, after the volume label
        entry = short_name(name) + b"\x20" + bytes(2) + bytes(2) + DATE.to_bytes(2, "little") \
            + DATE.to_bytes(2, "little") + (FIRST >> 16).to_bytes(2, "little") + bytes(2) \
            + DATE.to_bytes(2, "little") + (FIRST & 0xFFFF).to_bytes(2, "little") \
            + len(data).to_bytes(4, "little")
        assert len(entry) == 32
        f.seek(DATA * SECTOR + 32)
        f.write(entry)
        # the data
        f.seek((DATA + (FIRST - 2) * SECTORS_PER_CLUSTER) * SECTOR)
        f.write(data)
    print(f"{image}: {name.upper()} ({len(data)} bytes)")


if __name__ == "__main__":
    main()

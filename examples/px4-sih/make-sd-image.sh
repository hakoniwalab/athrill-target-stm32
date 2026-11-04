#!/bin/sh
# Makes sd.img, the emulated microSD card: 1 GB, FAT32 without a partition
# table (the PX4 logs, the mission data and a parameter backup go there).
#
# The image is the same, byte for byte, on every host: the FAT32 layout of
# macOS newfs_msdos (-F 32 -v PX4SD) for 1 GB, written by python3 with a
# fixed volume serial and label date. Host formatters do not agree
# (mkfs.vfat chooses another size of the FAT), and PX4 then runs
# differently: a run on another host could not be identical. The file is
# sparse; it is made under another name and renamed only when complete.
# Usage: make-sd-image.sh [image]  (default: sd.img next to this script;
# a relative path is taken from this script's directory)
set -e
cd "$(dirname "$0")"
OUT=${1:-sd.img}
if [ -e "$OUT" ]; then
	echo "$OUT exists"
	exit 0
fi
python3 - "$OUT.new" <<'EOF'
import sys

SECTOR = 512
TOTAL = 2097152          # sectors: 1 GiB
RESERVED = 32
FAT_SECTORS = 2044
SECTORS_PER_CLUSTER = 8
SERIAL = 0x20261104
DATA = RESERVED + 2 * FAT_SECTORS
CLUSTERS = (TOTAL - DATA) // SECTORS_PER_CLUSTER


def sector(content=b"", signature=False):
    s = bytearray(SECTOR)
    s[:len(content)] = content
    if signature:
        s[510:512] = b"\x55\xaa"
    return bytes(s)


boot = bytearray(bytes.fromhex(
    "eb58904253442020342e3400020820000200000000f000002000ff00000000000000"
    "2000fc070000000000000200000001000600000000000000000000000000000029"
    "0000000050583453442020202020204641543332202020"))
boot[67:71] = SERIAL.to_bytes(4, "little")
boot += bytes.fromhex(
    "fa31c08ed0bc007cfb8ed8e800005e83c619bb0700fcac84c07406b40ecd10ebf5"
    "30e4cd16cd190d0a4e6f6e2d73797374656d206469736b0d0a5072657373"
    "20616e79206b657920746f207265626f6f740d0a")
boot = sector(bytes(boot), True)

fsinfo = bytearray(SECTOR)
fsinfo[0:4] = b"RRaA"
fsinfo[484:488] = b"rrAa"
fsinfo[488:492] = (CLUSTERS - 1).to_bytes(4, "little")   # free: all but the root directory
fsinfo[492:496] = (3).to_bytes(4, "little")              # next free cluster
fsinfo[510:512] = b"\x55\xaa"
fsinfo = bytes(fsinfo)

fat = sector(bytes.fromhex("f0ffff0fffffff0fffffff0f"))
# the root directory: the volume label entry, its date fixed (2026-11-04 00:00)
label = b"PX4SD      " + b"\x28" + bytes(10) + (0).to_bytes(2, "little") + \
    (((2026 - 1980) << 9) | (11 << 5) | 4).to_bytes(2, "little") + bytes(6)
root = sector(label)

with open(sys.argv[1], "wb") as f:
    f.truncate(TOTAL * SECTOR)
    for number, content in ((0, boot), (1, fsinfo), (6, boot), (7, fsinfo),
                            (RESERVED, fat), (RESERVED + FAT_SECTORS, fat), (DATA, root)):
        f.seek(number * SECTOR)
        f.write(content)
EOF
mv "$OUT.new" "$OUT"
echo "$OUT: 1 GB FAT32"

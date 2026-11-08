#!/bin/sh
# Boot ArduCopter with live bench sensors and a writable microSD card.
# Every run starts from the same state: the F-RAM seed and a new, empty
# microSD image (the same bytes on every host).
# Usage: run-full.sh <arducopter.elf> [clock-limit] [athrill3]
set -e

if [ $# -lt 1 ]; then
	echo "usage: $0 <arducopter.elf> [clock-limit] [athrill3]" >&2
	exit 1
fi

ELF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
CLOCK_LIMIT=${2:--1}
DIR=$(cd "$(dirname "$0")" && pwd)
ATHRILL=${3:-$DIR/../../build/athrill3}
ATHRILL=$(cd "$(dirname "$ATHRILL")" && pwd)/$(basename "$ATHRILL")

# The board: Pixhawk 4's Devicetree in athrill-device (../boards.sh), added
# to a copy of the configuration (the path depends on the checkout).
EXAMPLES=$DIR/..
. "$EXAMPLES/boards.sh"
DTB=$(board_dtb pixhawk4 pixhawk4)
if [ ! -f "$DTB" ]; then
	echo "no $DTB: clone hakoniwalab/athrill-device next to this repository or set ATHRILL_DEVICE_DIR" >&2
	exit 1
fi

cd "$DIR"
cp fram-brd-io-disabled.bin fram.bin
rm -f sd.img
../px4-sih/make-sd-image.sh "$DIR/sd.img" > /dev/null
./make-mavlink-request.py mavlink-request.raw
{ cat device_config-full.txt; echo "DEVICE_CONFIG_DEVICE_TREE $DTB"; } > device_config-full-run.txt
exec "$ATHRILL" -c1 -m memory.txt -d device_config-full-run.txt -t "$CLOCK_LIMIT" "$ELF"

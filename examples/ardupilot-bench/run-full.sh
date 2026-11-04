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

cd "$DIR"
cp fram-brd-io-disabled.bin fram.bin
rm -f sd.img
../px4-sih/make-sd-image.sh "$DIR/sd.img" > /dev/null
./make-mavlink-request.py mavlink-request.raw
exec "$ATHRILL" -c1 -m memory.txt -d device_config-full.txt -t "$CLOCK_LIMIT" "$ELF"

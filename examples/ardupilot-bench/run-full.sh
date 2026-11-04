#!/bin/sh
# Boot ArduCopter with live bench sensors and a writable microSD card.
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
if [ ! -e fram.bin ]; then
	cp fram-brd-io-disabled.bin fram.bin
fi
if [ ! -e sd.img ]; then
	../px4-sih/make-sd-image.sh
	cp ../px4-sih/sd.img sd.img
fi
./make-mavlink-request.py mavlink-request.raw
exec "$ATHRILL" -c1 -m memory.txt -d device_config-full.txt -t "$CLOCK_LIMIT" "$ELF"

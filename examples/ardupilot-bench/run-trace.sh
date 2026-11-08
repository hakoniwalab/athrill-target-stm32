#!/bin/sh
# Write the first 20 million instruction PCs from the Round-2 workload.
# Usage: run-trace.sh <arducopter.elf> [athrill3]
set -e

if [ $# -lt 1 ]; then
	echo "usage: $0 <arducopter.elf> [athrill3]" >&2
	exit 1
fi

ELF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
DIR=$(cd "$(dirname "$0")" && pwd)
ATHRILL=${2:-$DIR/../../build/athrill3}
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
{ cat device_config-trace.txt; echo "DEVICE_CONFIG_DEVICE_TREE $DTB"; } > device_config-trace-run.txt
exec "$ATHRILL" -c1 -m memory.txt -d device_config-trace-run.txt -t 1000000000 "$ELF"

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

cd "$DIR"
cp fram-brd-io-disabled.bin fram.bin
exec "$ATHRILL" -c1 -m memory.txt -d device_config-trace.txt -t 1000000000 "$ELF"

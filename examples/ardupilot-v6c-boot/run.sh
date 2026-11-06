#!/bin/sh
# Boot ArduCopter Pixhawk6C in a bounded run, observing USART3 and H7 accesses.
# Usage: run.sh <arducopter.elf> [clock-limit] [athrill3]
set -e

if [ $# -lt 1 ]; then
	echo "usage: $0 <arducopter.elf> [clock-limit] [athrill3]" >&2
	exit 1
fi

ELF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
CLOCK_LIMIT=${2:-2000000000}
DIR=$(cd "$(dirname "$0")" && pwd)
ATHRILL=${3:-$DIR/../../build/athrill3}
ATHRILL=$(cd "$(dirname "$ATHRILL")" && pwd)/$(basename "$ATHRILL")

cd "$DIR"
exec "$ATHRILL" -c1 -m memory.txt -d device_config.txt -t "$CLOCK_LIMIT" "$ELF"

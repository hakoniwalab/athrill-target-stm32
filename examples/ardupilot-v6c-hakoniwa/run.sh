#!/bin/sh
# ArduCopter on the emulated Pixhawk 6C, flying the Hakoniwa Drone x500.
set -eu

if [ $# -ne 3 ]; then
	echo "usage: $0 <arducopter.elf> <athrill3> <libhako_sensor_device>" >&2
	exit 2
fi

absolute()
{
	printf '%s/%s\n' "$(cd "$(dirname "$1")" && pwd)" "$(basename "$1")"
}

ELF=$(absolute "$1")
ATHRILL=$(absolute "$2")
DEVICE=$(absolute "$3")
DIR=$(cd "$(dirname "$0")" && pwd)

"$DIR/prepare.sh" "$ELF" "$ATHRILL"
cd "$DIR"
{ cat memory.txt; printf 'DEV, 0x60000000, %s\n' "$DEVICE"; } >memory-hakoniwa.txt
echo "ArduCopter v6C on Athrill with Hakoniwa; start Hakoniwa, then connect to TCP 127.0.0.1:5760"
exec "$ATHRILL" -c1 -m memory-hakoniwa.txt -d device_config.txt -t -1 "$ELF"

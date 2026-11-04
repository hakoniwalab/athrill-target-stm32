#!/bin/sh
# PX4 SIH on the emulated Pixhawk 4:
#   examples/px4-sih/run.sh <px4_fmu-v5_default.elf> [athrill3]
# The first run makes the SD card image and selects the SIH airframe
# (stored in the emulated F-RAM, fram.bin); every run then boots PX4 with
# MAVLink on TCP 127.0.0.1:5760 for QGroundControl. Stop with Ctrl+C.
# Start again from scratch: rm fram.bin sd.img
set -e
if [ $# -lt 1 ]; then
	echo "usage: $0 <px4_fmu-v5_default.elf> [athrill3]" >&2
	exit 1
fi
ELF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
DIR=$(cd "$(dirname "$0")" && pwd)
ATHRILL=${2:-$DIR/../../build/athrill3}
ATHRILL=$(cd "$(dirname "$ATHRILL")" && pwd)/$(basename "$ATHRILL")
cd "$DIR"
./make-sd-image.sh
if [ ! -e fram.bin ]; then
	echo "first boot: SYS_AUTOSTART 1100 (SIH quadcopter X) into fram.bin ..."
	"$ATHRILL" -c1 -m memory.txt -d first-boot-config.txt -t 4000000000 "$ELF" > first-boot.log 2>&1 || true
	if ! grep -q "SYS_AUTOSTART: curr: 0 -> new: 1100" first-boot.log; then
		echo "first boot failed, see $DIR/first-boot.log" >&2
		rm -f fram.bin
		exit 1
	fi
fi
echo "PX4 on Athrill; connect QGroundControl to TCP 127.0.0.1:5760"
exec "$ATHRILL" -c1 -m memory.txt -d device_config.txt -t -1 "$ELF"

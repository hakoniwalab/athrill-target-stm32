#!/bin/sh
# PX4 with its own sensor drivers on the emulated Pixhawk 4, at rest on a bench:
#   examples/px4-bench/run.sh <px4_fmu-v5_default.elf> [athrill3]
# The sensors see the vehicle at rest at Tokyo Station (Athrill's sensor
# source without an external device). The first run makes the SD card image
# and selects the generic quadcopter airframe and the chips' calibration
# (stored in the emulated F-RAM, fram.bin); every run then boots PX4 with
# MAVLink on TCP 127.0.0.1:5760 for QGroundControl. Stop with Ctrl+C.
# The board is athrill-device boards/pixhawk4/pixhawk4.dtb (or
# DEVICE_CONFIG_DEVICE_TREE), as in check.sh.
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
. "$DIR/../boards.sh"
DTB=${DEVICE_CONFIG_DEVICE_TREE:-$(board_dtb pixhawk4 pixhawk4)}
if [ ! -f "$DTB" ]; then
	echo "no $DTB: clone hakoniwalab/athrill-device next to this repository or set ATHRILL_DEVICE_DIR" >&2
	exit 1
fi
DTB=$(cd "$(dirname "$DTB")" && pwd)/$(basename "$DTB")
cd "$DIR"
# the board from its Devicetree, added to each configuration
with_dtb() {	# configuration -> configuration for this run
	{ grep -v '^DEVICE_CONFIG_DEVICE_TREE ' "$1"; echo "DEVICE_CONFIG_DEVICE_TREE $DTB"; } > "$2"
}
./make-sd-image.sh
if [ ! -e fram.bin ]; then
	echo "first boot: SYS_AUTOSTART 4001 (generic quadcopter) and the chips' calibration IDs into fram.bin ..."
	with_dtb first-boot-config.txt first-boot-config-run.txt
	"$ATHRILL" -c1 -m memory.txt -d first-boot-config-run.txt -t 4000000000 "$ELF" > first-boot.log 2>&1 || true
	if ! grep -q "SYS_AUTOSTART: curr: 0 -> new: 4001" first-boot.log; then
		echo "first boot failed, see $DIR/first-boot.log" >&2
		rm -f fram.bin
		exit 1
	fi
fi
echo "PX4 on Athrill; connect QGroundControl to TCP 127.0.0.1:5760"
with_dtb device_config.txt device_config-run.txt
exec "$ATHRILL" -c1 -m memory.txt -d device_config-run.txt -t -1 "$ELF"

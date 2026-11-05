#!/bin/sh
# PX4 on the emulated Pixhawk 4, flying the Hakoniwa Drone x500 (docs/hakoniwa.md):
#   examples/px4-hakoniwa/run.sh <px4_fmu-v5_default.elf> <athrill3> <libhako_sensor_device>
# The first run makes the SD card image with the x500 parameters and runs them
# once, without Hakoniwa (the device gives a vehicle at rest); they are stored
# in the emulated F-RAM, fram.bin. Every run then boots PX4 on Hakoniwa, with
# MAVLink on TCP 127.0.0.1:5760. Start Hakoniwa Drone's aircraft service first,
# and `hako-cmd start` after this. Stop with Ctrl+C.
# Start again from scratch: rm fram.bin sd.img
set -e
if [ $# -lt 3 ]; then
	echo "usage: $0 <px4_fmu-v5_default.elf> <athrill3> <libhako_sensor_device>" >&2
	exit 1
fi
abs() { echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; }
ELF=$(abs "$1")
ATHRILL=$(abs "$2")
DEVICE=$(abs "$3")
DIR=$(cd "$(dirname "$0")" && pwd)
cd "$DIR"
{ cat memory.txt; echo "DEV, 0x60000000, $DEVICE"; } > memory-hakoniwa.txt
if [ ! -e sd.img ]; then
	rm -f sd.img.blank
	../px4-sih/make-sd-image.sh "$DIR/sd.img.blank"
	python3 put-sd-file.py sd.img.blank ../../docs/px4/x500-hakoniwa-params.sh X500.SH
	mv sd.img.blank sd.img
fi
if [ ! -e fram.bin ]; then
	echo "first boot: the x500 parameters into fram.bin (a few minutes) ..."
	"$ATHRILL" -c1 -m memory-hakoniwa.txt -d first-boot-config.txt -t 8640000000 "$ELF" > first-boot.log 2>&1 || true
	if ! grep -q "SYS_AUTOSTART: curr: 0 -> new: 4019" first-boot.log; then
		echo "first boot failed, see $DIR/first-boot.log" >&2
		rm -f fram.bin
		exit 1
	fi
fi
echo "PX4 on Athrill with Hakoniwa; run hako-cmd start, then connect to TCP 127.0.0.1:5760"
exec "$ATHRILL" -c1 -m memory-hakoniwa.txt -d device_config.txt -t -1 "$ELF"

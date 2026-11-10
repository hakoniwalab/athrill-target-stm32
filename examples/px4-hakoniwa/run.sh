#!/bin/sh
# PX4 on the emulated Pixhawk 4, flying the Hakoniwa Drone x500 (docs/hakoniwa.md):
#   examples/px4-hakoniwa/run.sh <px4_fmu-v5_default.elf> <athrill3> <libhako_sensor_device> [extra-config]
# extra-config: lines appended to the configuration of the flight (for example
# DEVICE_CONFIG_STM32_JIT 1 and DEVICE_CONFIG_DEVICE_TICK_SKIP 1).
# In process (no aircraft service, no hako-cmd): give libhako_sensor_device_inproc and set
# HAKO_DRONE_ROOT (the unpacked Hakoniwa Drone release) and HAKO_AIRCRAFT_CONFIG_PATH (the airframe).
# The first run makes the SD card image with the x500 parameters and runs them
# once, without Hakoniwa (the device gives a vehicle at rest); they are stored
# in the emulated F-RAM, fram.bin. Every run then boots PX4 on Hakoniwa, with
# MAVLink on TCP 127.0.0.1:5760. Start Hakoniwa Drone's aircraft service first,
# and `hako-cmd start` after this. Stop with Ctrl+C.
# Start again from scratch: rm fram.bin sd.img
set -e
if [ $# -lt 3 ]; then
	echo "usage: $0 <px4_fmu-v5_default.elf> <athrill3> <libhako_sensor_device> [extra-config]" >&2
	exit 1
fi
abs() { echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; }
ELF=$(abs "$1")
ATHRILL=$(abs "$2")
DEVICE=$(abs "$3")
EXTRA=
if [ $# -ge 4 ]; then
	EXTRA=$(abs "$4")
fi
INPROC=
if [ -n "${HAKO_DRONE_ROOT:-}" ]; then
	if [ -z "${HAKO_AIRCRAFT_CONFIG_PATH:-}" ]; then
		echo "HAKO_DRONE_ROOT is set: set HAKO_AIRCRAFT_CONFIG_PATH to the airframe too" >&2
		exit 1
	fi
	INPROC=1
	DRONE_ROOT=$(cd "$HAKO_DRONE_ROOT" && pwd)
	AIRFRAME=$(abs "$HAKO_AIRCRAFT_CONFIG_PATH")
fi
DIR=$(cd "$(dirname "$0")" && pwd)
cd "$DIR"
EXAMPLES=$(cd "$DIR/.." && pwd)
. "$EXAMPLES/boards.sh"
DTB=$(board_dtb pixhawk4 pixhawk4-hakoniwa)
if [ ! -f "$DTB" ]; then
	echo "no $DTB: clone hakoniwalab/athrill-device next to this repository or set ATHRILL_DEVICE_DIR" >&2
	exit 1
fi
# The board from its Devicetree, with the Hakoniwa sensor device as its plugin.
dt_config() {	# configuration -> configuration for this run
	{ grep -v -e '^DEVICE_CONFIG_DEVICE_TREE ' -e '^DEVICE_CONFIG_DEVICE_PLUGIN_0 ' "$1"
	  echo "DEVICE_CONFIG_DEVICE_TREE $DTB"
	  echo "DEVICE_CONFIG_DEVICE_PLUGIN_0 hakoniwa%2Csensor-device=$DEVICE"; } > "$2"
}
# The flight: in process, Athrill runs in the Drone release (the airframe names its model relative to it), so
# the board's files are given with absolute paths, and the aircraft is configured; then the extra lines.
flight_config() {	# configuration -> configuration of the flight
	dt_config "$1" "$2.tmp"
	if [ -n "$INPROC" ]; then
		sed -e "s|^DEVICE_CONFIG_STM32_FRAM_FILE fram.bin$|DEVICE_CONFIG_STM32_FRAM_FILE $DIR/fram.bin|" \
		    -e "s|^DEVICE_CONFIG_STM32_SDCARD_IMAGE sd.img$|DEVICE_CONFIG_STM32_SDCARD_IMAGE $DIR/sd.img|" "$2.tmp" > "$2"
		printf 'HAKO_AIRCRAFT_CONFIG_PATH %s\nHAKO_AIRCRAFT_WORKER 1\n' "$AIRFRAME" >> "$2"
	else
		cp "$2.tmp" "$2"
	fi
	rm -f "$2.tmp"
	if [ -n "$EXTRA" ]; then
		cat "$EXTRA" >> "$2"
	fi
}
flight_config device_config.txt device_config-run.txt
if [ ! -e sd.img ]; then
	rm -f sd.img.blank
	../px4-sih/make-sd-image.sh "$DIR/sd.img.blank"
	python3 put-sd-file.py sd.img.blank ../../docs/px4/x500-hakoniwa-params.sh X500.SH
	mv sd.img.blank sd.img
fi
if [ ! -e fram.bin ]; then
	echo "first boot: the x500 parameters into fram.bin (a few minutes) ..."
	dt_config first-boot-config.txt first-boot-config-run.txt
	"$ATHRILL" -c1 -m memory.txt -d first-boot-config-run.txt -t 8640000000 "$ELF" > first-boot.log 2>&1 || true
	if ! grep -q "SYS_AUTOSTART: curr: 0 -> new: 4019" first-boot.log; then
		echo "first boot failed, see $DIR/first-boot.log" >&2
		rm -f fram.bin
		exit 1
	fi
fi
if [ -n "$INPROC" ]; then
	echo "PX4 on Athrill with the aircraft in process; connect to TCP 127.0.0.1:5760"
	cd "$DRONE_ROOT"
	exec "$ATHRILL" -c1 -m "$DIR/memory.txt" -d "$DIR/device_config-run.txt" -t -1 "$ELF"
fi
echo "PX4 on Athrill with Hakoniwa; run hako-cmd start, then connect to TCP 127.0.0.1:5760"
exec "$ATHRILL" -c1 -m memory.txt -d device_config-run.txt -t -1 "$ELF"

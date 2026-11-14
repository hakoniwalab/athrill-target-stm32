#!/bin/sh
# The official ArduCopter Pixhawk6C firmware on the emulated Pixhawk 6C, flying the Hakoniwa Drone x500
# (docs/hakoniwa.md, "ArduPilot"):
#   examples/ardupilot-v6c-hakoniwa/run.sh <arducopter.elf> <athrill3> <libhako_sensor_device> [extra-config]
# The same flight as examples/ardupilot-hakoniwa (the x500 parameters, the F-RAM seed, load-params.py, fly.py and
# the airframe are used from there) on the board athrill-device boards/pixhawk6c (pixhawk6c-hakoniwa.dtb), with
# the CPU at 400 MHz: ArduPilot's Pixhawk6C build runs its H743 at 400 MHz (DShot frames are timed from it).
# extra-config: lines appended to the configuration of the flight (for example
# DEVICE_CONFIG_STM32_JIT 1 and DEVICE_CONFIG_DEVICE_TICK_SKIP 1).
# In process (no aircraft service, no hako-cmd): give libhako_sensor_device_inproc and set
# HAKO_DRONE_ROOT (the unpacked Hakoniwa Drone release) and HAKO_AIRCRAFT_CONFIG_PATH (the airframe, e.g.
# examples/ardupilot-hakoniwa/x500-athrill-ardupilot).
# The first run starts from the F-RAM seed (BRD_IO_ENABLE=0) and an empty SD card, boots ArduCopter on the test
# vehicle (no Hakoniwa), writes the x500 parameters over MAVLink (needs pymavlink) and stops: the parameters stay
# in the emulated F-RAM, fram.bin. Every run then boots ArduCopter on Hakoniwa, with MAVLink on TCP
# 127.0.0.1:5760. Start Hakoniwa Drone's aircraft service first, and `hako-cmd start` after this.
# Stop with Ctrl+C. Start again from scratch: rm fram.bin sd.img
set -e
if [ $# -lt 3 ]; then
	echo "usage: $0 <arducopter.elf> <athrill3> <libhako_sensor_device> [extra-config]" >&2
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
PWM=$EXAMPLES/ardupilot-hakoniwa
. "$EXAMPLES/boards.sh"
DTB=$(board_dtb pixhawk6c pixhawk6c-hakoniwa)
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
if [ ! -e fram.bin ]; then
	rm -f sd.img
	../px4-sih/make-sd-image.sh "$DIR/sd.img" > /dev/null
	cp "$PWM/fram-seed.bin" fram.bin.new
	sed 's/^DEVICE_CONFIG_STM32_FRAM_FILE fram.bin$/DEVICE_CONFIG_STM32_FRAM_FILE fram.bin.new/' first-boot-config.txt > first-boot-config.tmp
	dt_config first-boot-config.tmp first-boot-config-run.txt
	echo "first boot: the x500 parameters into fram.bin (about 2 minutes) ..."
	"$ATHRILL" -c1 -m memory.txt -d first-boot-config-run.txt -t -1 "$ELF" > first-boot.log 2>&1 &
	PID=$!
	if python3 "$PWM/load-params.py" "$PWM/x500-hakoniwa-ardupilot.parm" > load-params.log 2>&1; then
		kill "$PID"
		wait "$PID" 2> /dev/null || true
		mv fram.bin.new fram.bin
		tail -1 load-params.log
	else
		kill "$PID"
		wait "$PID" 2> /dev/null || true
		rm -f fram.bin.new
		echo "first boot failed, see $DIR/load-params.log and $DIR/first-boot.log" >&2
		exit 1
	fi
fi
if [ -n "$INPROC" ]; then
	echo "ArduCopter on Athrill with the aircraft in process; connect to TCP 127.0.0.1:5760"
	cd "$DRONE_ROOT"
	exec "$ATHRILL" -c1 -m "$DIR/memory.txt" -d "$DIR/device_config-run.txt" -t -1 "$ELF"
fi
echo "ArduCopter on Athrill with Hakoniwa; run hako-cmd start, then connect to TCP 127.0.0.1:5760"
exec "$ATHRILL" -c1 -m memory.txt -d device_config-run.txt -t -1 "$ELF"

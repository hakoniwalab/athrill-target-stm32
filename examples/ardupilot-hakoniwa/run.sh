#!/bin/sh
# The official ArduCopter Pixhawk4 firmware on the emulated Pixhawk 4, flying the Hakoniwa Drone x500
# (docs/hakoniwa.md, "ArduPilot"):
#   examples/ardupilot-hakoniwa/run.sh <arducopter.elf> <athrill3> <libhako_sensor_device> [extra-config]
# extra-config: lines appended to the configuration of the flight (for example
# DEVICE_CONFIG_STM32_JIT 1 and DEVICE_CONFIG_DEVICE_TICK_SKIP 1).
# In process (no aircraft service, no hako-cmd): give libhako_sensor_device_inproc and set
# HAKO_DRONE_ROOT (the unpacked Hakoniwa Drone release) and HAKO_AIRCRAFT_CONFIG_PATH (the airframe).
# The first run starts from fram-seed.bin (BRD_IO_ENABLE=0) and an empty SD card, boots ArduCopter on the test
# vehicle (no Hakoniwa), writes x500-hakoniwa-ardupilot.parm over MAVLink (load-params.py, needs pymavlink) and
# stops: the parameters stay in the emulated F-RAM, fram.bin. Every run then boots ArduCopter on Hakoniwa, with
# MAVLink on TCP 127.0.0.1:5760. Start Hakoniwa Drone's aircraft service first, and `hako-cmd start` after this.
# Stop with Ctrl+C. Set ATHRILL_WORK_DIR to keep generated F-RAM, SD-card,
# configuration and log files outside this source directory.
set -e
PYTHON=${PYTHON:-python3}
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
WORK_DIR=${ATHRILL_WORK_DIR:-$DIR}
mkdir -p "$WORK_DIR"
WORK_DIR=$(cd "$WORK_DIR" && pwd)
if [ -n "$INPROC" ]; then
	AIRFRAME_SOURCE=$AIRFRAME
	AIRFRAME=$WORK_DIR/aircraft-config
	mkdir -p "$AIRFRAME"
	AIRFRAME_FOUND=
	for AIRFRAME_JSON in "$AIRFRAME_SOURCE"/drone_config_*.json; do
		[ -f "$AIRFRAME_JSON" ] || continue
		cp "$AIRFRAME_JSON" "$AIRFRAME/"
		AIRFRAME_FOUND=1
	done
	if [ -z "$AIRFRAME_FOUND" ]; then
		echo "no drone_config_*.json in $AIRFRAME_SOURCE" >&2
		exit 1
	fi
fi
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
	sed -e "s|^DEVICE_CONFIG_STM32_FRAM_FILE fram.bin$|DEVICE_CONFIG_STM32_FRAM_FILE $WORK_DIR/fram.bin|" \
	    -e "s|^DEVICE_CONFIG_STM32_SDCARD_IMAGE sd.img$|DEVICE_CONFIG_STM32_SDCARD_IMAGE $WORK_DIR/sd.img|" \
	    "$2.tmp" > "$2"
	if [ -n "$INPROC" ]; then
		printf 'HAKO_AIRCRAFT_CONFIG_PATH %s\nHAKO_AIRCRAFT_WORKER 1\n' "$AIRFRAME" >> "$2"
	fi
	rm -f "$2.tmp"
	if [ -n "$EXTRA" ]; then
		cat "$EXTRA" >> "$2"
	fi
}
flight_config "$DIR/device_config.txt" "$WORK_DIR/device_config-run.txt"
if [ ! -e "$WORK_DIR/fram.bin" ]; then
	rm -f "$WORK_DIR/sd.img"
	"$EXAMPLES/px4-sih/make-sd-image.sh" "$WORK_DIR/sd.img" > /dev/null
	cp "$DIR/fram-seed.bin" "$WORK_DIR/fram.bin.new"
	sed -e "s|^DEVICE_CONFIG_STM32_FRAM_FILE fram.bin$|DEVICE_CONFIG_STM32_FRAM_FILE $WORK_DIR/fram.bin.new|" \
	    -e "s|^DEVICE_CONFIG_STM32_SDCARD_IMAGE sd.img$|DEVICE_CONFIG_STM32_SDCARD_IMAGE $WORK_DIR/sd.img|" \
	    "$DIR/first-boot-config.txt" > "$WORK_DIR/first-boot-config.tmp"
	dt_config "$WORK_DIR/first-boot-config.tmp" "$WORK_DIR/first-boot-config-run.txt"
	rm -f "$WORK_DIR/first-boot-config.tmp"
	echo "first boot: the x500 parameters into fram.bin (about 2 minutes) ..."
	"$ATHRILL" -c1 -m "$DIR/memory.txt" -d "$WORK_DIR/first-boot-config-run.txt" -t -1 "$ELF" > "$WORK_DIR/first-boot.log" 2>&1 &
	PID=$!
	if "$PYTHON" "$DIR/load-params.py" "$DIR/x500-hakoniwa-ardupilot.parm" > "$WORK_DIR/load-params.log" 2>&1; then
		kill "$PID"
		wait "$PID" 2> /dev/null || true
		mv "$WORK_DIR/fram.bin.new" "$WORK_DIR/fram.bin"
		tail -1 "$WORK_DIR/load-params.log"
	else
		kill "$PID"
		wait "$PID" 2> /dev/null || true
		rm -f "$WORK_DIR/fram.bin.new"
		echo "first boot failed, see $WORK_DIR/load-params.log and $WORK_DIR/first-boot.log" >&2
		exit 1
	fi
fi
if [ "${ATHRILL_PREPARE_ONLY:-0}" = 1 ]; then
	echo "ArduCopter state is ready in $WORK_DIR"
	exit 0
fi
# ATHRILL_OPTIONS: options given to athrill3 before the ELF, e.g.
# "--hakoniwa athrill --pdudef <file> --real-sleep-msec 10" to make Athrill the
# Hakoniwa asset/conductor (the in-process motor sensor device then publishes the
# x500's visual state for a browser viewer). Empty by default.
if [ -n "$INPROC" ]; then
	echo "ArduCopter on Athrill with the aircraft in process; connect to TCP 127.0.0.1:5760"
	if [ ! -e "$WORK_DIR/config" ] && [ ! -L "$WORK_DIR/config" ]; then
		ln -s "$DRONE_ROOT/config" "$WORK_DIR/config"
	fi
	cd "$WORK_DIR"
	exec "$ATHRILL" ${ATHRILL_OPTIONS:-} -c1 -m "$DIR/memory.txt" -d "$WORK_DIR/device_config-run.txt" -t -1 "$ELF"
fi
echo "ArduCopter on Athrill with Hakoniwa; run hako-cmd start, then connect to TCP 127.0.0.1:5760"
cd "$WORK_DIR"
exec "$ATHRILL" ${ATHRILL_OPTIONS:-} -c1 -m "$DIR/memory.txt" -d "$WORK_DIR/device_config-run.txt" -t -1 "$ELF"

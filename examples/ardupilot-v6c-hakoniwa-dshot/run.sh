#!/bin/sh
# The official ArduCopter Pixhawk6C firmware on the emulated Pixhawk 6C, driving the x500's motors with DShot600
# through emulated ESCs (docs/dshot.md, "ArduPilot"):
#   examples/ardupilot-v6c-hakoniwa-dshot/run.sh <arducopter.elf> <athrill3> <libhako_sensor_device...> [extra-config]
# The same flight as examples/ardupilot-v6c-hakoniwa (its configuration files are used from there), with the board
# athrill-device boards/pixhawk6c/pixhawk6c-hakoniwa-dshot.dtb (the ESCs on TIM1, their telemetry on USART2) and the
# x500 parameters plus dshot.parm (MOT_PWM_TYPE 6: DShot600; SERIAL5_PROTOCOL 16: ESC telemetry), written by the
# first run into this directory's fram.bin. The motor-stop check is examples/ardupilot-hakoniwa-dshot/fly-motor-fault.py.
# extra-config: lines appended to the configuration of the flight, e.g. DEVICE_CONFIG_ESC_MOTOR_STOP 2@70000
# (stops motor 3 at 70 s of vehicle time) or DEVICE_CONFIG_DSHOT_ESC_FILE <file> (one line per ESC frame).
# In process: set HAKO_DRONE_ROOT and HAKO_AIRCRAFT_CONFIG_PATH as for examples/ardupilot-v6c-hakoniwa.
# Stop with Ctrl+C. Start again from scratch: rm fram.bin sd.img
set -e
if [ $# -lt 3 ]; then
	echo "usage: $0 <arducopter.elf> <athrill3> <libhako_sensor_device...> [extra-config]" >&2
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
PWM=$EXAMPLES/ardupilot-v6c-hakoniwa
SHARED=$EXAMPLES/ardupilot-hakoniwa
. "$EXAMPLES/boards.sh"
DTB=$(board_dtb pixhawk6c pixhawk6c-hakoniwa-dshot)
if [ ! -f "$DTB" ]; then
	echo "no $DTB: use athrill-device with the DShot ESC (docs/dshot.md) or set ATHRILL_DEVICE_DIR" >&2
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
flight_config "$PWM/device_config.txt" device_config-run.txt
if [ ! -e fram.bin ]; then
	rm -f sd.img
	../px4-sih/make-sd-image.sh "$DIR/sd.img" > /dev/null
	cp "$SHARED/fram-seed.bin" fram.bin.new
	sed 's/^DEVICE_CONFIG_STM32_FRAM_FILE fram.bin$/DEVICE_CONFIG_STM32_FRAM_FILE fram.bin.new/' "$PWM/first-boot-config.txt" > first-boot-config.tmp
	dt_config first-boot-config.tmp first-boot-config-run.txt
	cat "$SHARED/x500-hakoniwa-ardupilot.parm" dshot.parm > x500-dshot.parm
	echo "first boot: the x500 and DShot parameters into fram.bin (about 2 minutes) ..."
	"$ATHRILL" -c1 -m "$PWM/memory.txt" -d first-boot-config-run.txt -t -1 "$ELF" > first-boot.log 2>&1 &
	PID=$!
	if python3 "$SHARED/load-params.py" x500-dshot.parm > load-params.log 2>&1; then
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
	echo "ArduCopter on Athrill with DShot and the aircraft in process; connect to TCP 127.0.0.1:5760"
	cd "$DRONE_ROOT"
	exec "$ATHRILL" -c1 -m "$PWM/memory.txt" -d "$DIR/device_config-run.txt" -t -1 "$ELF"
fi
echo "ArduCopter on Athrill with DShot and Hakoniwa; run hako-cmd start, then connect to TCP 127.0.0.1:5760"
exec "$ATHRILL" -c1 -m "$PWM/memory.txt" -d device_config-run.txt -t -1 "$ELF"

#!/bin/sh
# PX4 on the emulated Pixhawk 4 driving the x500's motors with DShot600 through emulated ESCs (docs/dshot.md):
#   examples/px4-hakoniwa-dshot/run.sh <px4_fmu-v5_default.elf> <athrill3> <libhako_sensor_device...> [extra-config]
# The same flight as examples/px4-hakoniwa (its configuration files are used from there), with:
# - the board athrill-device boards/pixhawk4/pixhawk4-hakoniwa-dshot.dtb (the ESCs on TIM1, their KISS telemetry
#   on UART4); DSHOT_BOARD names another one of that directory, e.g. pixhawk4-hakoniwa-dshot-asset;
# - the x500 parameters plus dshot-params.sh (PWM_AUX_TIM0 -3: DShot600; DSHOT_TEL_CFG 104: telemetry on TELEM4),
#   written by the first run into this directory's fram.bin;
# - ATHRILL_OPTIONS: options given to athrill3 before the ELF, e.g. "--hakoniwa athrill --pdudef <file>".
# extra-config: lines appended to the configuration of the flight, e.g. DEVICE_CONFIG_ESC_MOTOR_STOP 2@40000
# (stops motor 3 at 40 s) or DEVICE_CONFIG_DSHOT_ESC_FILE <file> (one line per ESC frame).
# In process: set HAKO_DRONE_ROOT and HAKO_AIRCRAFT_CONFIG_PATH as for examples/px4-hakoniwa.
# Start again from scratch: rm fram.bin sd.img
set -e
if [ $# -lt 3 ]; then
	echo "usage: $0 <px4_fmu-v5_default.elf> <athrill3> <libhako_sensor_device...> [extra-config]" >&2
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
PWM=$EXAMPLES/px4-hakoniwa
. "$EXAMPLES/boards.sh"
DTB=$(board_dtb pixhawk4 "${DSHOT_BOARD:-pixhawk4-hakoniwa-dshot}")
if [ ! -f "$DTB" ]; then
	echo "no $DTB: use athrill-device with the DShot ESC (docs/dshot.md) or set ATHRILL_DEVICE_DIR" >&2
	exit 1
fi
# The board from its Devicetree, with the Hakoniwa sensor device as its plugin; the console input of the
# first boot from examples/px4-hakoniwa.
dt_config() {	# configuration -> configuration for this run
	{ grep -v -e '^DEVICE_CONFIG_DEVICE_TREE ' -e '^DEVICE_CONFIG_DEVICE_PLUGIN_0 ' "$1" |
	    sed -e "s|^DEVICE_CONFIG_STM32_UART_RX_FILE first-boot.nsh$|DEVICE_CONFIG_STM32_UART_RX_FILE $PWM/first-boot.nsh|"
	  echo "DEVICE_CONFIG_DEVICE_TREE $DTB"
	  echo "DEVICE_CONFIG_DEVICE_PLUGIN_0 hakoniwa%2Csensor-device=$DEVICE"; } > "$2"
}
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
if [ ! -e sd.img ]; then
	rm -f sd.img.blank
	"$EXAMPLES/px4-sih/make-sd-image.sh" "$DIR/sd.img.blank"
	# the x500 parameters with the DShot lines before their final "param save"
	{ grep -v '^param save$' "$EXAMPLES/../docs/px4/x500-hakoniwa-params.sh"; cat dshot-params.sh; } > x500.sh.tmp
	python3 "$PWM/put-sd-file.py" sd.img.blank x500.sh.tmp X500.SH
	rm -f x500.sh.tmp
	mv sd.img.blank sd.img
fi
if [ ! -e fram.bin ]; then
	echo "first boot: the x500 and DShot parameters into fram.bin (a few minutes) ..."
	dt_config "$PWM/first-boot-config.txt" first-boot-config-run.txt
	"$ATHRILL" -c1 -m "$PWM/memory.txt" -d first-boot-config-run.txt -t 8640000000 "$ELF" > first-boot.log 2>&1 || true
	if ! grep -q "SYS_AUTOSTART: curr: 0 -> new: 4019" first-boot.log \
		|| ! grep -q "PWM_AUX_TIM0: curr: .* -> new: -3" first-boot.log; then
		echo "first boot failed, see $DIR/first-boot.log" >&2
		rm -f fram.bin
		exit 1
	fi
fi
# shellcheck disable=SC2086
if [ -n "$INPROC" ]; then
	echo "PX4 on Athrill with DShot and the aircraft in process; connect to TCP 127.0.0.1:5760"
	cd "$DRONE_ROOT"
	exec "$ATHRILL" ${ATHRILL_OPTIONS:-} -c1 -m "$PWM/memory.txt" -d "$DIR/device_config-run.txt" -t -1 "$ELF"
fi
echo "PX4 on Athrill with DShot and Hakoniwa; run hako-cmd start, then connect to TCP 127.0.0.1:5760"
exec "$ATHRILL" ${ATHRILL_OPTIONS:-} -c1 -m "$PWM/memory.txt" -d device_config-run.txt -t -1 "$ELF"

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
# Set ATHRILL_WORK_DIR to keep generated F-RAM, SD-card, configuration and log
# files outside this source directory.
set -e
PYTHON=${PYTHON:-python3}
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
flight_config "$PWM/device_config.txt" "$WORK_DIR/device_config-run.txt"
if [ ! -e "$WORK_DIR/sd.img" ]; then
	rm -f "$WORK_DIR/sd.img.blank"
	"$EXAMPLES/px4-sih/make-sd-image.sh" "$WORK_DIR/sd.img.blank"
	# the x500 parameters with the DShot lines before their final "param save"
	{ grep -v '^param save$' "$EXAMPLES/../docs/px4/x500-hakoniwa-params.sh"; cat "$DIR/dshot-params.sh"; } > "$WORK_DIR/x500.sh.tmp"
	"$PYTHON" "$PWM/put-sd-file.py" "$WORK_DIR/sd.img.blank" "$WORK_DIR/x500.sh.tmp" X500.SH
	rm -f "$WORK_DIR/x500.sh.tmp"
	mv "$WORK_DIR/sd.img.blank" "$WORK_DIR/sd.img"
fi
if [ ! -e "$WORK_DIR/fram.bin" ]; then
	echo "first boot: the x500 and DShot parameters into fram.bin (a few minutes) ..."
	# Create the backing image explicitly so preparation always leaves a complete
	# state bundle, including when PX4 restores its parameter backup from SD.
	# A zeroed FM25V02 image is the device's erased state.
	dd if=/dev/zero of="$WORK_DIR/fram.bin" bs=32768 count=1 2>/dev/null
	dt_config "$PWM/first-boot-config.txt" "$WORK_DIR/first-boot-config.tmp"
	sed -e "s|^DEVICE_CONFIG_STM32_FRAM_FILE fram.bin$|DEVICE_CONFIG_STM32_FRAM_FILE $WORK_DIR/fram.bin|" \
	    -e "s|^DEVICE_CONFIG_STM32_SDCARD_IMAGE sd.img$|DEVICE_CONFIG_STM32_SDCARD_IMAGE $WORK_DIR/sd.img|" \
	    "$WORK_DIR/first-boot-config.tmp" > "$WORK_DIR/first-boot-config-run.txt"
	rm -f "$WORK_DIR/first-boot-config.tmp"
	"$ATHRILL" -c1 -m "$PWM/memory.txt" -d "$WORK_DIR/first-boot-config-run.txt" -t 8640000000 "$ELF" > "$WORK_DIR/first-boot.log" 2>&1 || true
	if grep -aq "Parameter .* not found" "$WORK_DIR/first-boot.log" \
		|| { ! { grep -q "SYS_AUTOSTART: curr: 0 -> new: 4019" "$WORK_DIR/first-boot.log" \
			&& grep -q "PWM_AUX_TIM0: curr: .* -> new: -3" "$WORK_DIR/first-boot.log"; } \
			&& ! { grep -Eq "BSON_INT32: +SYS_AUTOSTART = 4019" "$WORK_DIR/first-boot.log" \
				&& grep -Eq "BSON_INT32: +PWM_AUX_TIM0 = -3" "$WORK_DIR/first-boot.log"; }; }; then
		echo "first boot failed, see $WORK_DIR/first-boot.log" >&2
		rm -f "$WORK_DIR/fram.bin"
		exit 1
	fi
fi
if [ "${ATHRILL_PREPARE_ONLY:-0}" = 1 ]; then
	echo "PX4 DShot state is ready in $WORK_DIR"
	exit 0
fi
# shellcheck disable=SC2086
if [ -n "$INPROC" ]; then
	echo "PX4 on Athrill with DShot and the aircraft in process; connect to TCP 127.0.0.1:5760"
	if [ ! -e "$WORK_DIR/config" ] && [ ! -L "$WORK_DIR/config" ]; then
		ln -s "$DRONE_ROOT/config" "$WORK_DIR/config"
	fi
	cd "$WORK_DIR"
	exec "$ATHRILL" ${ATHRILL_OPTIONS:-} -c1 -m "$PWM/memory.txt" -d "$WORK_DIR/device_config-run.txt" -t -1 "$ELF"
fi
echo "PX4 on Athrill with DShot and Hakoniwa; run hako-cmd start, then connect to TCP 127.0.0.1:5760"
cd "$WORK_DIR"
exec "$ATHRILL" ${ATHRILL_OPTIONS:-} -c1 -m "$PWM/memory.txt" -d "$WORK_DIR/device_config-run.txt" -t -1 "$ELF"

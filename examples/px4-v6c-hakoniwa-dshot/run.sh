#!/bin/sh
# PX4 on the emulated Pixhawk 6C driving the x500's motors with DShot600 through emulated ESCs (docs/dshot.md):
#   examples/px4-v6c-hakoniwa-dshot/run.sh <px4_fmu-v6c_default.elf> <athrill3> <libhako_sensor_device...> [extra-config]
# The configuration files of examples/px4-v6c-hakoniwa are used from there, with:
# - the board athrill-device boards/pixhawk6c/pixhawk6c-hakoniwa-dshot.dtb (the ESCs on TIM1, their KISS telemetry
#   on USART2);
# - the x500 parameters, the 6C sensor IDs and dshot-params.sh (PWM_AUX_TIM0 -3: DShot600; DSHOT_TEL_CFG 103:
#   telemetry on TELEM3), written by the first run into this directory's fram.bin, on the bench board (no Hakoniwa).
# extra-config: lines appended to the configuration of the flight, e.g. DEVICE_CONFIG_ESC_MOTOR_STOP 2@40000
# (stops motor 3 at 40 s) or DEVICE_CONFIG_DSHOT_ESC_FILE <file> (one line per ESC frame).
# In process: set HAKO_DRONE_ROOT and HAKO_AIRCRAFT_CONFIG_PATH as for examples/px4-hakoniwa.
# MAVLink on TCP 127.0.0.1:5760 (TELEM1, UART7). Start again from scratch: rm fram.bin sd.img
set -e
if [ $# -lt 3 ]; then
	echo "usage: $0 <px4_fmu-v6c_default.elf> <athrill3> <libhako_sensor_device...> [extra-config]" >&2
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
V6C=$EXAMPLES/px4-v6c-hakoniwa
. "$EXAMPLES/boards.sh"
DTB=$(board_dtb pixhawk6c pixhawk6c-hakoniwa-dshot)
if [ ! -f "$DTB" ]; then
	echo "no $DTB: use athrill-device with the 6C DShot board (docs/dshot.md) or set ATHRILL_DEVICE_DIR" >&2
	exit 1
fi
if [ ! -e sd.img ]; then
	rm -f sd.img.blank
	"$EXAMPLES/px4-sih/make-sd-image.sh" "$DIR/sd.img.blank" > /dev/null
	# the x500 parameters, the 6C sensor IDs, then the DShot lines (each part ends with "param save")
	cat "$EXAMPLES/../docs/px4/x500-hakoniwa-params.sh" "$V6C/x500-v6c-params.sh" dshot-params.sh > x500.sh.tmp
	python3 "$EXAMPLES/px4-hakoniwa/put-sd-file.py" sd.img.blank x500.sh.tmp X500.SH > /dev/null
	rm -f x500.sh.tmp
	mv sd.img.blank sd.img
fi
if [ ! -e fram.bin ]; then
	echo "first boot: the x500, 6C and DShot parameters into fram.bin (a few minutes) ..."
	# The test vehicle of the Hakoniwa sensor device at rest where the x500 starts (first-boot-config.txt, as
	# examples/px4-hakoniwa's first boot): PX4 stores its barometer offset and declination for that place.
	{ sed -e "s|^DEVICE_CONFIG_STM32_UART_RX_FILE first-boot.nsh$|DEVICE_CONFIG_STM32_UART_RX_FILE $V6C/first-boot.nsh|" \
	    first-boot-config.txt
	  echo "DEVICE_CONFIG_DEVICE_TREE $DTB"
	  echo "DEVICE_CONFIG_DEVICE_PLUGIN_0 hakoniwa%2Csensor-device=$DEVICE"; } > first-boot-config-run.txt
	"$ATHRILL" -c1 -m "$V6C/memory.txt" -d first-boot-config-run.txt -t 100000000000 "$ELF" > first-boot.log 2>&1 || true
	if ! grep -aq "SYS_AUTOSTART: curr: 0 -> new: 4019" first-boot.log \
		|| ! grep -aq "CAL_ACC0_ID: curr: .* -> new: 4259850" first-boot.log \
		|| ! grep -aq "PWM_AUX_TIM0: curr: .* -> new: -3" first-boot.log; then
		echo "first boot failed, see $DIR/first-boot.log" >&2
		rm -f fram.bin
		exit 1
	fi
fi
# The flight: the board from its Devicetree with the Hakoniwa sensor device as its plugin; in process, Athrill runs
# in the Drone release (the airframe names its model relative to it), so the board's files are given with absolute
# paths, and the aircraft is configured; then the extra lines.
{ grep -v -e '^DEVICE_CONFIG_STM32_FRAM_FILE ' -e '^DEVICE_CONFIG_STM32_SDCARD_IMAGE ' "$V6C/device_config.txt"
  echo "DEVICE_CONFIG_STM32_FRAM_FILE $DIR/fram.bin"
  echo "DEVICE_CONFIG_STM32_SDCARD_IMAGE $DIR/sd.img"
  echo "DEVICE_CONFIG_DEVICE_TREE $DTB"
  echo "DEVICE_CONFIG_DEVICE_PLUGIN_0 hakoniwa%2Csensor-device=$DEVICE"
  if [ -n "$INPROC" ]; then
	printf 'HAKO_AIRCRAFT_CONFIG_PATH %s\nHAKO_AIRCRAFT_WORKER 1\n' "$AIRFRAME"
  fi
  if [ -n "$EXTRA" ]; then
	cat "$EXTRA"
  fi; } > device_config-run.txt
if [ -n "$INPROC" ]; then
	echo "PX4 on the Pixhawk 6C with DShot and the aircraft in process; connect to TCP 127.0.0.1:5760"
	cd "$DRONE_ROOT"
	exec "$ATHRILL" -c1 -m "$V6C/memory.txt" -d "$DIR/device_config-run.txt" -t -1 "$ELF"
fi
echo "PX4 on the Pixhawk 6C with DShot and Hakoniwa; run hako-cmd start, then connect to TCP 127.0.0.1:5760"
exec "$ATHRILL" -c1 -m "$V6C/memory.txt" -d device_config-run.txt -t -1 "$ELF"

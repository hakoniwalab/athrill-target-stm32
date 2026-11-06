#!/bin/sh
# Compare an armed PX4 FMUv6C flight on the legacy C/EXDEV and DT/plugin paths.
# No TCP listener is configured; NSH commands arrive through the UART RX file.
set -eu

if [ $# -ne 8 ]; then
	echo "usage: $0 PX4_ELF ATHRILL3 EXDEV_DEVICE MODEL_PLUGIN HAKO_DRONE_ROOT AIRFRAME_CONFIG DTB WORK" >&2
	exit 2
fi

absolute()
{
	case $1 in
	/*) printf '%s\n' "$1" ;;
	*) printf '%s/%s\n' "$(cd "$(dirname "$1")" && pwd)" "$(basename "$1")" ;;
	esac
}

ELF=$(absolute "$1")
ATHRILL=$(absolute "$2")
EXDEV=$(absolute "$3")
PLUGIN=$(absolute "$4")
DRONE_ROOT=$(absolute "$5")
AIRFRAME=$(absolute "$6")
DTB=$(absolute "$7")
WORK=$(absolute "$8")
DIR=$(cd "$(dirname "$0")" && pwd)

for path in "$ELF" "$ATHRILL" "$EXDEV" "$PLUGIN" "$DTB"; do
	[ -f "$path" ] || { echo "missing file: $path" >&2; exit 2; }
done
[ -d "$DRONE_ROOT" ] || { echo "missing Drone root: $DRONE_ROOT" >&2; exit 2; }
[ -e "$AIRFRAME" ] || { echo "missing airframe: $AIRFRAME" >&2; exit 2; }

mkdir -p "$WORK"
"$DIR/prepare.sh" "$ELF" "$ATHRILL" >"$WORK/prepare.log" 2>&1

prepare_run()
{
	path=$1
	mkdir -p "$path"
	cp "$DIR/memory.txt" "$DIR/device_config.txt" "$DIR/fram.bin" "$DIR/sd.img" \
		"$DIR/armed.nsh" "$path/"
	awk -v fram="$path/fram.bin" -v sd="$path/sd.img" \
		-v rx="$path/armed.nsh" -v tx="$path/console.raw" \
		-v pwm="$path/pwm.trace" '
		$1 == "DEVICE_CONFIG_STM32_FRAM_FILE" { print $1, fram; next }
		$1 == "DEVICE_CONFIG_STM32_SDCARD_IMAGE" { print $1, sd; next }
		$1 == "DEVICE_CONFIG_STM32_UART7_TCP_PORT" { next }
		$1 == "DEVICE_CONFIG_STM32_UART_RX_FILE" { next }
		$1 == "DEVICE_CONFIG_STM32_UART_RX_DELAY_MS" { next }
		$1 == "DEVICE_CONFIG_STM32_UART_TX_FILE" { next }
		$1 == "DEVICE_CONFIG_STM32_ACTUATOR_FILE" { next }
		$1 == "DEVICE_CONFIG_DEVICE_TREE" { next }
		$1 == "DEVICE_CONFIG_DEVICE_PLUGIN_0" { next }
		{ print }
		END {
			print "DEVICE_CONFIG_STM32_UART_RX_FILE", rx
			print "DEVICE_CONFIG_STM32_UART_RX_DELAY_MS 50000"
			print "DEVICE_CONFIG_STM32_UART_TX_FILE", tx
			print "DEVICE_CONFIG_STM32_ACTUATOR_FILE", pwm
		}
	' "$path/device_config.txt" >"$path/device_config.base"
}

run_c()
{
	path=$WORK/c
	status=0
	prepare_run "$path"
	cp "$path/device_config.base" "$path/device_config.run"
	printf 'HAKO_AIRCRAFT_CONFIG_PATH %s\nHAKO_AIRCRAFT_WORKER 1\nHAKO_SENSOR_DEVICE_TRACE_PATH %s\n' \
		"$AIRFRAME" "$path/sensor-device.trace" >>"$path/device_config.run"
	printf 'DEV, 0x60000000, %s\n' "$EXDEV" >>"$path/memory.txt"
	(
		cd "$DRONE_ROOT"
		"$ATHRILL" -c1 -m "$path/memory.txt" -d "$path/device_config.run" \
			-t 43200000000 "$ELF"
	) >"$path/emulator.log" 2>&1 || status=$?
	status=${status:-0}
	[ "$status" -eq 0 ] || [ "$status" -eq 1 ] || return "$status"
}

run_dt()
{
	path=$WORK/dt
	status=0
	prepare_run "$path"
	cp "$path/device_config.base" "$path/device_config.run"
	printf 'DEVICE_CONFIG_DEVICE_TREE %s\n' "$DTB" >>"$path/device_config.run"
	printf 'DEVICE_CONFIG_DEVICE_PLUGIN_0 hakoniwa%%2Csensor-device=%s\n' "$PLUGIN" >>"$path/device_config.run"
	printf 'HAKO_AIRCRAFT_CONFIG_PATH %s\nHAKO_AIRCRAFT_WORKER 1\nHAKO_SENSOR_DEVICE_TRACE_PATH %s\n' \
		"$AIRFRAME" "$path/sensor-device.trace" >>"$path/device_config.run"
	(
		cd "$DRONE_ROOT"
		"$ATHRILL" -c1 -m "$path/memory.txt" -d "$path/device_config.run" \
			-t 43200000000 "$ELF"
	) >"$path/emulator.log" 2>&1 || status=$?
	status=${status:-0}
	[ "$status" -eq 0 ] || [ "$status" -eq 1 ] || return "$status"
}

run_c
run_dt

grep -a "Armed by .* command" "$WORK/c/console.raw"
grep -a "Armed by .* command" "$WORK/dt/console.raw"
grep -a "Takeoff detected" "$WORK/c/console.raw"
grep -a "Takeoff detected" "$WORK/dt/console.raw"

# The two implementations include different provider labels in the PWM trace.
# Preserve normalized files for locating the first raw PWM sampling difference.
sed -E 's/ timer=[0-9]+ channel=/ channel=/' "$WORK/c/pwm.trace" >"$WORK/c/pwm-values.trace"
cp "$WORK/dt/pwm.trace" "$WORK/dt/pwm-values.trace"
if ! cmp "$WORK/c/pwm-values.trace" "$WORK/dt/pwm-values.trace"; then
	echo "NOTE: raw PWM transition traces differ; comparing aircraft sample values" >&2
fi
python3 "$DIR/compare-armed-traces.py" \
	"$WORK/c/sensor-device.trace" "$WORK/dt/sensor-device.trace"

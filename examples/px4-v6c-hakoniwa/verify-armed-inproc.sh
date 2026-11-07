#!/bin/sh
# Compare an armed PX4 FMUv6C flight exactly on the legacy C/EXDEV and DT/plugin paths.
# No TCP listener is configured; NSH commands arrive through the UART RX file.
# Optional diagnostics: ATHRILL_TICKS, ARM_PC_TRACE_COUNT,
# ARM_PC_TRACE_SKIP, ARM_PC_TRACE_CLOCKS and VERIFY_ARMED=0.
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
TICKS=${ATHRILL_TICKS:-43200000000}
PC_TRACE_COUNT=${ARM_PC_TRACE_COUNT:-0}
PC_TRACE_SKIP=${ARM_PC_TRACE_SKIP:-0}
PC_TRACE_CLOCKS=${ARM_PC_TRACE_CLOCKS:-0}
VERIFY_ARMED=${VERIFY_ARMED:-1}

case $TICKS:$PC_TRACE_COUNT:$PC_TRACE_SKIP:$PC_TRACE_CLOCKS:$VERIFY_ARMED in
*[!0-9:]*|*:*:*:*:*:*)
	echo "ATHRILL_TICKS and ARM_PC_TRACE_* and VERIFY_ARMED must be decimal integers" >&2
	exit 2
	;;
esac

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
	if [ "$PC_TRACE_COUNT" -ne 0 ]; then
		printf 'DEVICE_CONFIG_ARM_PC_TRACE_FILE %s\nDEVICE_CONFIG_ARM_PC_TRACE_COUNT %s\n' \
			"$path/pc.trace" "$PC_TRACE_COUNT" >>"$path/device_config.run"
		printf 'DEVICE_CONFIG_ARM_PC_TRACE_SKIP %s\n' "$PC_TRACE_SKIP" >>"$path/device_config.run"
		printf 'DEVICE_CONFIG_ARM_PC_TRACE_CLOCKS %s\n' "$PC_TRACE_CLOCKS" >>"$path/device_config.run"
	fi
	printf 'DEV, 0x60000000, %s\n' "$EXDEV" >>"$path/memory.txt"
	(
		cd "$DRONE_ROOT"
		"$ATHRILL" -c1 -m "$path/memory.txt" -d "$path/device_config.run" \
			-t "$TICKS" "$ELF"
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
	if [ "$PC_TRACE_COUNT" -ne 0 ]; then
		printf 'DEVICE_CONFIG_ARM_PC_TRACE_FILE %s\nDEVICE_CONFIG_ARM_PC_TRACE_COUNT %s\n' \
			"$path/pc.trace" "$PC_TRACE_COUNT" >>"$path/device_config.run"
		printf 'DEVICE_CONFIG_ARM_PC_TRACE_SKIP %s\n' "$PC_TRACE_SKIP" >>"$path/device_config.run"
		printf 'DEVICE_CONFIG_ARM_PC_TRACE_CLOCKS %s\n' "$PC_TRACE_CLOCKS" >>"$path/device_config.run"
	fi
	(
		cd "$DRONE_ROOT"
		"$ATHRILL" -c1 -m "$path/memory.txt" -d "$path/device_config.run" \
			-t "$TICKS" "$ELF"
	) >"$path/emulator.log" 2>&1 || status=$?
	status=${status:-0}
	[ "$status" -eq 0 ] || [ "$status" -eq 1 ] || return "$status"
}

run_c
run_dt

if [ "$PC_TRACE_COUNT" -ne 0 ]; then
	cmp "$WORK/c/pc.trace" "$WORK/dt/pc.trace"
fi

if [ "$VERIFY_ARMED" -eq 0 ]; then
	exit 0
fi

grep -a "Armed by .* command" "$WORK/c/console.raw"
grep -a "Armed by .* command" "$WORK/dt/console.raw"
grep -a "Takeoff detected" "$WORK/c/console.raw"
grep -a "Takeoff detected" "$WORK/dt/console.raw"

# The two implementations include different provider labels in the PWM trace.
# Normalize that label only; every value and transition must then be identical.
sed -E 's/ timer=[0-9]+ channel=/ channel=/' "$WORK/c/pwm.trace" >"$WORK/c/pwm-values.trace"
cp "$WORK/dt/pwm.trace" "$WORK/dt/pwm-values.trace"
cmp "$WORK/c/pwm-values.trace" "$WORK/dt/pwm-values.trace"
cmp "$WORK/c/sensor-device.trace" "$WORK/dt/sensor-device.trace"
cmp "$WORK/c/console.raw" "$WORK/dt/console.raw"
cmp "$WORK/c/fram.bin" "$WORK/dt/fram.bin"
cmp "$WORK/c/sd.img" "$WORK/dt/sd.img"
C_COUNT=$(sed -n 's/^SAMPLES .* steps=//p' "$WORK/c/emulator.log" | tail -1)
DT_COUNT=$(sed -n 's/^SAMPLES .* steps=//p' "$WORK/dt/emulator.log" | tail -1)
[ -n "$C_COUNT" ] && [ "$C_COUNT" = "$DT_COUNT" ] || {
	echo "instruction count differs: C=${C_COUNT:-missing} DT=${DT_COUNT:-missing}" >&2
	exit 1
}
python3 "$DIR/compare-armed-traces.py" \
	"$WORK/c/sensor-device.trace" "$WORK/dt/sensor-device.trace"
echo "PASS: armed C/DT traces and state are byte-identical ($C_COUNT instructions)"

# The Devicetree run against the board's regression reference in athrill-device.
. "$DIR/../regression.sh"
echo "regression reference (athrill-device boards/pixhawk6c/regression/armed.txt)"
regression_check "$(regression_reference pixhawk6c armed)" "$ELF" "$DT_COUNT" "$WORK/dt"

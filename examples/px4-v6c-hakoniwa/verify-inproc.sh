#!/bin/sh
# Two deterministic, disarmed, 60-second v6C runs with the in-process aircraft.
set -eu

if [ $# -ne 6 ]; then
	echo "usage: $0 PX4_ELF ATHRILL3 INPROC_DEVICE HAKO_DRONE_ROOT AIRFRAME_CONFIG INPROC_RUNNER" >&2
	exit 2
fi

absolute()
{
	printf '%s/%s\n' "$(cd "$(dirname "$1")" && pwd)" "$(basename "$1")"
}

ELF=$(absolute "$1")
ATHRILL=$(absolute "$2")
DEVICE=$(absolute "$3")
DRONE_ROOT=$(absolute "$4")
AIRFRAME=$(absolute "$5")
RUNNER=$(absolute "$6")
DIR=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/athrill-px4-v6c-inproc.XXXXXX")
cleanup()
{
	status=$?
	if [ "$status" -eq 0 ]; then
		rm -r "$WORK"
	else
		echo "failed artifacts: $WORK" >&2
	fi
}
trap cleanup EXIT
trap 'exit 130' HUP INT TERM

"$DIR/prepare.sh" "$ELF" "$ATHRILL"

run_once()
{
	run=$1
	mkdir -p "$WORK/$run"
	cp "$DIR/memory.txt" "$DIR/device_config.txt" "$DIR/fram.bin" "$DIR/sd.img" "$DIR/ekf.nsh" "$WORK/$run/"
	awk -v rx="$WORK/$run/ekf.nsh" -v tx="$WORK/$run/console.raw" '
		$1 == "DEVICE_CONFIG_STM32_UART7_TCP_PORT" { next }
		$1 == "DEVICE_CONFIG_STM32_UART_RX_FILE" { next }
		$1 == "DEVICE_CONFIG_STM32_UART_RX_DELAY_MS" { next }
		$1 == "DEVICE_CONFIG_STM32_UART_TX_FILE" { next }
		{ print }
		END {
			print "DEVICE_CONFIG_STM32_UART_RX_FILE", rx
			print "DEVICE_CONFIG_STM32_UART_RX_DELAY_MS 50000"
			print "DEVICE_CONFIG_STM32_UART_TX_FILE", tx
		}
	' "$WORK/$run/device_config.txt" >"$WORK/$run/device_config.new"
	mv "$WORK/$run/device_config.new" "$WORK/$run/device_config.txt"
	set +e
	ATHRILL_TICKS=28800000000 HAKO_SENSOR_DEVICE_TRACE_PATH="$WORK/$run/sensor-device.trace" \
		"$RUNNER" "$WORK/$run" "$ELF" "$ATHRILL" "$DEVICE" "$DRONE_ROOT" "$AIRFRAME" \
		>"$WORK/$run/emulator.log" 2>&1
	status=$?
	set -e
	[ "$status" -eq 0 ] || [ "$status" -eq 1 ] || {
		echo "FAIL: $run emulator exit $status" >&2
		exit 1
	}
	grep -aq "CAL_ACC0_ID.*4259850" "$WORK/$run/console.raw"
	grep -aq "CAL_GYRO0_ID.*4325386" "$WORK/$run/console.raw"
	grep -aq "CAL_MAG0_ID.*396321" "$WORK/$run/console.raw"
	grep -aq "filter_fault_flags: 0" "$WORK/$run/console.raw"
	grep -aq "xy_valid: True" "$WORK/$run/console.raw"
	grep -aq "z_valid: True" "$WORK/$run/console.raw"
	count=$(grep -ac '^sensor ' "$WORK/$run/sensor-device.trace")
	[ "$count" -ge 14000 ] || {
		echo "FAIL: $run has only $count sensor trace records" >&2
		exit 1
	}
	echo "$run: PX4 EKF converged, $count sensor records"
}

run_once run1
run_once run2
cmp "$WORK/run1/sensor-device.trace" "$WORK/run2/sensor-device.trace"
echo "PASS: PX4 v6C in-process trace identical twice"

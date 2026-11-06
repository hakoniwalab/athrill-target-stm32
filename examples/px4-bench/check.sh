#!/bin/sh
# The PX4 sensor drivers on the emulated chips, without external physics:
#   examples/px4-bench/check.sh <px4_fmu-v5_default.elf> [athrill3]
#
# PX4 boots as a generic quadcopter (SYS_AUTOSTART 4001) with its own drivers
# for the emulated ICM-20689, MS5611, IST8310 and u-blox, which see a bench
# (at rest at Tokyo Station, with fixed-seed noise and a GPS fix; Athrill's
# sensor source without an external device). After 30 s it checks, in PX4's
# own reports: the IMU driver's transfers, the sensor validators, the GPS fix,
# EKF2's position, the preflight check; then that a second run from the same
# F-RAM and SD card images gives the same console output and SD card image.
# Work files: examples/px4-bench/work. Exit status 0 when all pass.
# The checks read PX4's console output (device IDs, `listener` fields) as
# PX4 v1.17.0 prints it; another PX4 version may word or number it
# differently.
set -e
if [ $# -lt 1 ]; then
	echo "usage: $0 <px4_fmu-v5_default.elf> [athrill3]" >&2
	exit 1
fi
ELF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
ATHRILL=${2:-$HERE/../../build/athrill3}
ATHRILL=$(cd "$(dirname "$ATHRILL")" && pwd)/$(basename "$ATHRILL")
WORK=$HERE/work
FAILED=0

# Optional DT-mode gate.  Resolve it before changing into a work directory so
# the copied configuration is independent of the caller's current directory.
DTB=
PLUGIN=
if [ -n "${DEVICE_CONFIG_DEVICE_TREE:-}" ]; then
	if [ ! -f "$DEVICE_CONFIG_DEVICE_TREE" ]; then
		echo "DEVICE_CONFIG_DEVICE_TREE is not a file: $DEVICE_CONFIG_DEVICE_TREE" >&2
		exit 1
	fi
	DTB=$(cd "$(dirname "$DEVICE_CONFIG_DEVICE_TREE")" && pwd)/$(basename "$DEVICE_CONFIG_DEVICE_TREE")
	case "$DTB" in *' '*)
		echo "DEVICE_CONFIG_DEVICE_TREE cannot contain spaces: $DTB" >&2
		exit 1;;
	esac
fi
if [ -n "${DEVICE_CONFIG_DEVICE_PLUGIN_0:-}" ]; then
	PLUGIN=$DEVICE_CONFIG_DEVICE_PLUGIN_0
	plugin_path=${PLUGIN#*=}
	if [ "$plugin_path" = "$PLUGIN" ] || [ ! -f "$plugin_path" ]; then
		echo "DEVICE_CONFIG_DEVICE_PLUGIN_0 must be encoded-compatible=library-file" >&2
		exit 1
	fi
fi

append_device_tree() {
	if [ -n "$DTB" ]; then
		echo "DEVICE_CONFIG_DEVICE_TREE $DTB" >> "$1"
	fi
	if [ -n "$PLUGIN" ]; then
		echo "DEVICE_CONFIG_DEVICE_PLUGIN_0 $PLUGIN" >> "$1"
	fi
}

clone() {
	cp -c "$1" "$2" 2>/dev/null || cp --reflink=auto "$1" "$2" 2>/dev/null || cp "$1" "$2"
}

check() {	# name, pattern (grep -E), file
	if grep -aEq "$2" "$3"; then
		echo "  [PASS] $1"
	else
		echo "  [FAIL] $1 (no \"$2\")"
		FAILED=1
	fi
}

rm -rf "$WORK"
mkdir -p "$WORK/golden"
cd "$WORK/golden"
cp "$HERE/memory.txt" "$HERE/first-boot.nsh" "$HERE/make-sd-image.sh" .
grep -v UART2_TCP "$HERE/first-boot-config.txt" > first-boot-config.txt
append_device_tree first-boot-config.txt
./make-sd-image.sh > /dev/null
echo "first boot: SYS_AUTOSTART 4001 and the chips' calibration IDs"
"$ATHRILL" -c1 -m memory.txt -d first-boot-config.txt -t 4000000000 "$ELF" > first-boot.log 2>&1 || true
grep -q "SYS_AUTOSTART: curr: 0 -> new: 4001" first-boot.log

for run in run1 run2; do
	mkdir -p "$WORK/$run"
	cd "$WORK/$run"
	cp "$HERE/memory.txt" "$HERE/bench.nsh" .
	{ cat "$HERE/device_config.txt" | grep -v UART2_TCP; echo "DEVICE_CONFIG_STM32_UART_RX_FILE bench.nsh"; } > config.txt
	append_device_tree config.txt
	clone "$WORK/golden/fram.bin" fram.bin
	clone "$WORK/golden/sd.img" sd.img
	("$ATHRILL" -c1 -m memory.txt -d config.txt -t 12000000000 "$ELF" > console.txt 2> stderr.txt || true) &
done
wait
cd "$WORK"
L=run1/console.txt
echo "the drivers and the estimator (PX4's own reports, run 1)"
for run in run1 run2; do
	check "$run ran to its clock limit" "EXIT for timeout" $run/console.txt
done
check "airframe 4001 loaded" "Loading airframe: .*4001_quad_x" $L
check "ICM-20689 (SPI1): no bad transfers" "icm20689: bad transfer: 0 events" $L
check "IMU at 800 Hz" "selected sensor: 3932170, rate: 800.0 Hz" $L
check "gyroscope selected, validator OK" "selected gyro: 3932170" $L
check "accelerometer selected" "selected accel: 3932170" $L
check "IST8310 (I2C3) selected" "selected MAG: 396825" $L
check "MS5611 (SPI4) selected" "selected BARO: 3997730" $L
check "no sensor validator in failsafe" "failsafe: NO" $L
if grep -aq "failsafe: YES" $L; then
	echo "  [FAIL] a sensor validator is in failsafe"
	FAILED=1
fi
check "u-blox (USART1): 3D fix" "fix_type: 3" $L
check "EKF2 horizontal position valid" "xy_valid: True" $L
check "EKF2 vertical position valid" "z_valid: True" $L
check "EKF2 origin at the bench (Tokyo Station)" "ref_lat: 35.681236" $L
check "preflight check OK" "Preflight check: OK" $L
echo "determinism"
if cmp -s run1/console.txt run2/console.txt && cmp -s run1/sd.img run2/sd.img && cmp -s run1/fram.bin run2/fram.bin; then
	echo "  [PASS] run1 = run2: console, F-RAM and SD card image identical ($(grep -a 'steps=' run1/stderr.txt | sed 's/.*steps=//') instructions)"
else
	echo "  [FAIL] run1 != run2"
	FAILED=1
fi
if [ $FAILED = 0 ]; then
	echo "ALL PASS"
else
	echo "SOME CHECKS FAILED"
fi
exit $FAILED

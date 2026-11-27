#!/bin/sh
# Checks PX4 SIH on the emulated Pixhawk 4 (README "Bit-identical, and checked"):
#   examples/px4-sih/verify/verify.sh [--step-a] <px4_fmu-v5_default.elf> [athrill3]
# --step-a runs only the first boot and A, with no TCP listener.
#
# A. Determinism: the same flight (scenario.nsh: take off, hover, land,
#    typed into the console at fixed virtual times) three times from the
#    same F-RAM and SD card images: twice in the default fast mode, once in
#    strict mode (Arm pseudo-code only). The F-RAM and the whole SD card
#    image (with the ULog), normalized console and instruction count must
#    be identical.
# B. The flight log of A: EKF2, attitude and position control
#    (analyze_ulog.py).
# C. With pymavlink: arm, take off, "go to" 5 m north, hover, land over
#    MAVLink as QGroundControl does (fly_scenario.py), and its log; with
#    matplotlib, its charts in work/charts (plot_ulog.py).
# D. With the release ELF (PX4 v1.17.0 from this repository's Release):
#    the whole SD card image, ULog, F-RAM, normalized console and
#    instruction count of A must match the published run on any host.
#
# Step A needs python3. The full run also needs numpy and pyulog, pymavlink
# for C, matplotlib for the charts, and mtools on Linux to read the SD card.
# Work files are in examples/px4-sih/verify/work.
# Exit status: 0 ALL PASS; 1 SOME CHECKS FAILED; 2 PARTIAL PASS (A and B
# passed, C skipped for want of pymavlink).
set -e
STEP_A_ONLY=0
if [ "${1:-}" = --step-a ]; then
	STEP_A_ONLY=1
	shift
fi
if [ $# -lt 1 ] || [ $# -gt 2 ]; then
	echo "usage: $0 [--step-a] <px4_fmu-v5_default.elf> [athrill3]" >&2
	exit 1
fi
ELF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
HERE=$(cd "$(dirname "$0")" && pwd)
EXAMPLE=$(dirname "$HERE")
ATHRILL=${2:-$EXAMPLE/../../build/athrill3}
ATHRILL=$(cd "$(dirname "$ATHRILL")" && pwd)/$(basename "$ATHRILL")
WORK=$HERE/work
FAILED=0
SKIPPED=0

# The board's Devicetree (athrill-device boards/pixhawk4/pixhawk4.dtb, or
# DEVICE_CONFIG_DEVICE_TREE). Resolve it before changing into a work directory
# so every generated config points at the same DTB from any caller directory.
EXAMPLES=$(dirname "$EXAMPLE")
. "$EXAMPLES/boards.sh"
DTB=
DEVICE_CONFIG_DEVICE_TREE=${DEVICE_CONFIG_DEVICE_TREE:-$(board_dtb pixhawk4 pixhawk4)}
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

append_device_tree() {
	if [ -n "$DTB" ]; then
		echo "DEVICE_CONFIG_DEVICE_TREE $DTB" >> "$1"
	fi
}

# Strip Athrill's configuration echoes and Devicetree hash from stdout.
# The JIT statistics go to stderr, which is checked separately for errors.
normalize_console() {
	sed -e '/^DEVICE_CONFIG_[A-Z0-9_]* = /d' \
	    -e '/^DEVICE_TREE sha256=/d' "$1"
}

instruction_count() {
	sed -n 's/^SAMPLES halted=[0-9][0-9]* steps=\([0-9][0-9]*\)$/\1/p' "$1"
}

# copy a file (a clone where the file system can)
clone() {
	cp -c "$1" "$2" 2>/dev/null || cp --reflink=auto "$1" "$2" 2>/dev/null || cp "$1" "$2"
}

# the newest ULog of an SD card image to $2
ulog_of() {
	if command -v mcopy > /dev/null 2>&1; then
		log=$(mdir -/ -b -i "$1" ::/log 2>/dev/null | grep '\.ulg$' | sort | tail -1)
		mcopy -o -i "$1" "$log" "$2"
	elif [ "$(uname -s)" = Darwin ]; then
		out=$(hdiutil attach -readonly -imagekey diskimage-class=CRawDiskImage "$1" 2>/dev/null)
		mp=$(echo "$out" | grep -o '/Volumes/.*' | head -1)
		dev=$(echo "$out" | awk '/dev/{print $1}' | head -1)
		cp "$(find "$mp/log" -name '*.ulg' | sort | tail -1)" "$2"
		hdiutil detach "$dev" > /dev/null 2>&1
	else
		echo "mcopy (mtools) is needed to read the SD card image" >&2
		return 1
	fi
}

sha256() {
	if command -v sha256sum > /dev/null 2>&1; then
		sha256sum "$1" | cut -d' ' -f1
	else
		shasum -a 256 "$1" | cut -d' ' -f1
	fi
}

# which firmware the results belong to (PX4 embeds build information: each build differs)
ELF_SHA=$(sha256 "$ELF")
echo "ELF sha256: $ELF_SHA"

# The published run of the release ELF (PX4 v1.17.0, built on the Mac): the
# whole SD card image, ULog, F-RAM and console of flight A. (make-sd-image.sh
# makes the same image on every host.)
# Re-recorded 2026-11-08 when the SDMMC became an athrill-device model
# (hakoniwalab/athrill-device#13 step 4-6): the F7 SDMMC's write DMA stream
# now completes when it has filled the FIFO, before DATAEND, as on the chip
# (before: at the end of the bus time). The flight differs from the release
# run from that timing on (shown by restoring the old timing: the old values
# return); every determinism and flight check passes in 3 runs.
# Re-recorded 2026-11-08: the UART receives whenever UE and RE are set, as
# on the chip, and the scenario is typed from 1 s on (from 0 s PX4's console
# setup drops the first character); every determinism and flight check passes.
# Re-recorded 2026-11-14 (SD card image, ULog) for athrill-device 6b6f588:
# CAN1 and CAN2 acknowledge initialization (athrill-device#104), so PX4's
# UAVCAN starts instead of failing (ErrMsrInakNotSet) and runs every 3 ms.
# F-RAM and console are unchanged; every determinism, flight-log and MAVLink
# flight check passes. With athrill-device 60fd5e0 the old values return.
# Re-recorded 2026-11-15 (all four): the Pixhawk 4 board gained the BMI055
# (accelerometer and gyroscope on SPI1, as on the real board), so PX4 runs
# two IMUs; every determinism, flight-log and MAVLink flight check passes.
# With the board without the BMI055 the old values return.
# docs/verification.md keeps the release run's values.
PUBLISHED_ELF=f8a78a13f3a545305cce45eeef677e4d436325ee6a6065b1753c387283f3d05e
PUBLISHED_STEPS=2904813819
PUBLISHED_ULOG=c8ad46d873743959c4954bfeff4a6d6215af7054c58ed0ea557e730fd1d06a22
PUBLISHED_FRAM=44e8812fac951efba3852303948d8e6d8330cb1297c530fc22b913e924646325
# Console after normalize_console.
PUBLISHED_CONSOLE=0ffcb425db7d1a40ce0e2c1d0684ee8b7d7a5f38fab4bed8aa49af256bad0fe5
PUBLISHED_SD=fdf844face8f088aa1d600fb4b794c5dbe6e9aead36cce465859aeff43b4dc1b

rm -rf "$WORK"
mkdir -p "$WORK/golden"
cd "$WORK/golden"
cp "$EXAMPLE/memory.txt" "$EXAMPLE/first-boot.nsh" .
sed '/^DEVICE_CONFIG_STM32_UART2_TCP_/d' "$EXAMPLE/first-boot-config.txt" > first-boot-config.txt
append_device_tree first-boot-config.txt
cp "$EXAMPLE/make-sd-image.sh" . && ./make-sd-image.sh > /dev/null
echo "golden images: first boot (SYS_AUTOSTART 1100)"
"$ATHRILL" -c1 -m memory.txt -d first-boot-config.txt -t 4000000000 "$ELF" > first-boot.log 2>&1 || true
grep -q "SYS_AUTOSTART: curr: 0 -> new: 1100" first-boot.log

echo "A. determinism: 2 runs fast, 1 run strict, in parallel"
for run in fast1 fast2 strict; do
	mkdir -p "$WORK/$run"
	cd "$WORK/$run"
	cp "$EXAMPLE/memory.txt" "$HERE/scenario.nsh" .
	if [ "$run" = strict ]; then
		sed -e '/^DEVICE_CONFIG_STM32_UART2_TCP_/d' \
		    -e '/^DEVICE_CONFIG_STM32_JIT[[:space:]]/d' \
		    -e '/^DEVICE_CONFIG_DEVICE_TICK_SKIP[[:space:]]/d' "$EXAMPLE/device_config.txt" > config.txt
		echo "DEVICE_CONFIG_ARM_FP_MODE strict" >> config.txt
		echo "DEVICE_CONFIG_ARM_EXEC_MODE strict" >> config.txt
	else
		sed '/^DEVICE_CONFIG_STM32_UART2_TCP_/d' "$EXAMPLE/device_config.txt" > config.txt
	fi
	echo "DEVICE_CONFIG_STM32_UART_RX_FILE scenario.nsh" >> config.txt
	echo "DEVICE_CONFIG_STM32_UART_RX_DELAY_MS 1000" >> config.txt
	append_device_tree config.txt
	clone "$WORK/golden/fram.bin" fram.bin
	clone "$WORK/golden/sd.img" sd.img
	("$ATHRILL" -c1 -m memory.txt -d config.txt -t 16000000000 "$ELF" > console.txt 2> stderr.txt || true) &
done
wait
cd "$WORK"
for run in fast1 fast2 strict; do
	if ! grep -aq "EXIT for timeout" "$run/console.txt"; then
		echo "  [FAIL] $run did not run to its clock limit"
		FAILED=1
	fi
	normalize_console "$run/console.txt" > "$run/console.normalized.txt"
done
STEPS=$(instruction_count fast1/stderr.txt)
for run in fast2 strict; do
	steps=$(instruction_count "$run/stderr.txt")
	if [ -n "$STEPS" ] && [ "$STEPS" = "$steps" ] &&
		cmp -s fast1/console.normalized.txt "$run/console.normalized.txt" &&
		cmp -s fast1/fram.bin "$run/fram.bin" && cmp -s fast1/sd.img "$run/sd.img"; then
		echo "  [PASS] fast1 = $run: F-RAM, SD card image and normalized console identical ($steps instructions)"
	else
		echo "  [FAIL] fast1 != $run (instruction counts: ${STEPS:-missing} vs ${steps:-missing})"
		FAILED=1
	fi
done

echo "console (athrill-device boards/pixhawk4/regression/px4.allow)"
if ! python3 "$EXAMPLES/console-check.py" "$(athrill_device_dir)/boards/pixhawk4/regression/px4.allow" \
	golden/first-boot.log fast1/console.txt fast2/console.txt strict/console.txt \
	fast1/stderr.txt fast2/stderr.txt strict/stderr.txt; then
	FAILED=1
fi

if [ "$STEP_A_ONLY" = 1 ]; then
	if [ "$FAILED" != 0 ]; then
		echo "STEP A FAILED"
		exit 1
	fi
	echo "STEP A PASS"
	exit 0
fi

echo "B. flight log of A"
ulog_of fast1/sd.img a.ulg
python3 "$HERE/analyze_ulog.py" a.ulg || FAILED=1

if python3 -c "import pymavlink" 2> /dev/null; then
	echo "C. MAVLink: take off, go to 5 m north, hover, land"
	mkdir -p "$WORK/mavlink"
	cd "$WORK/mavlink"
	cp "$EXAMPLE/memory.txt" "$EXAMPLE/device_config.txt" .
	append_device_tree device_config.txt
	clone "$WORK/golden/fram.bin" fram.bin
	clone "$WORK/golden/sd.img" sd.img
	"$ATHRILL" -c1 -m memory.txt -d device_config.txt -t -1 "$ELF" > console.txt 2>&1 &
	PID=$!
	python3 "$HERE/fly_scenario.py" tcp:127.0.0.1:5760 --north 5 || FAILED=1
	kill $PID 2> /dev/null || true
	wait $PID 2> /dev/null || true
	ulog_of sd.img c.ulg
	python3 "$HERE/analyze_ulog.py" c.ulg --target-north 5 --target-east 0 || FAILED=1
	if python3 -c "import matplotlib" 2> /dev/null; then
		python3 "$HERE/plot_ulog.py" c.ulg "$WORK/charts" --target-north 5 --target-east 0
	fi
else
	echo "C. SKIPPED: pymavlink is not installed (pip install pymavlink)"
	SKIPPED=1
fi

cd "$WORK"
if [ "$ELF_SHA" != "$PUBLISHED_ELF" ] && [ "${REGRESSION_STRICT:-0}" = 1 ]; then
	echo "D. [FAIL] not the release ELF, so the published run cannot be compared (REGRESSION_STRICT=1)"
	FAILED=1
fi
if [ "$ELF_SHA" = "$PUBLISHED_ELF" ]; then
	echo "D. the same as the published run (release ELF)"
	if [ "$STEPS" = "$PUBLISHED_STEPS" ] && [ "$(sha256 fast1/sd.img)" = $PUBLISHED_SD ] && [ "$(sha256 a.ulg)" = $PUBLISHED_ULOG ] && [ "$(sha256 fast1/fram.bin)" = $PUBLISHED_FRAM ] && [ "$(sha256 fast1/console.normalized.txt)" = $PUBLISHED_CONSOLE ]; then
		echo "  [PASS] SD card image, ULog, F-RAM, normalized console and instruction count match the published run (SD card image sha256 ${PUBLISHED_SD%"${PUBLISHED_SD#????????????????}"}...)"
	else
		echo "  [FAIL] differs from the published run: SD card image $(sha256 fast1/sd.img), ULog $(sha256 a.ulg), F-RAM $(sha256 fast1/fram.bin), console $(sha256 fast1/console.normalized.txt), instructions ${STEPS:-missing}"
		FAILED=1
	fi
fi

if [ $FAILED != 0 ]; then
	echo "SOME CHECKS FAILED"
	exit 1
fi
if [ $SKIPPED != 0 ]; then
	echo "PARTIAL PASS: A and B passed, C was skipped"
	exit 2
fi
echo "ALL PASS"
exit 0

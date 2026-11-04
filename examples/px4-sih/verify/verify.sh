#!/bin/sh
# Checks PX4 SIH on the emulated Pixhawk 4 (README "Verification"):
#   examples/px4-sih/verify/verify.sh <px4_fmu-v5_default.elf> [athrill3]
#
# A. Determinism: the same flight (scenario.nsh: take off, hover, land,
#    typed into the console at fixed virtual times) three times from the
#    same F-RAM and SD card images: twice in the default fast mode, once in
#    strict mode (Arm pseudo-code only). The F-RAM and the whole SD card
#    image (with the ULog) must be identical, and so must the console output
#    apart from the two DEVICE_CONFIG_ARM_* lines Athrill echoes at start in
#    the strict run (its configuration has them, the others do not).
# B. The flight log of A: EKF2, attitude and position control
#    (analyze_ulog.py).
# C. With pymavlink: arm, take off, "go to" 5 m north, hover, land over
#    MAVLink as QGroundControl does (fly_scenario.py), and its log; with
#    matplotlib, its charts in work/charts (plot_ulog.py).
# D. With the release ELF (PX4 v1.17.0 from this repository's Release):
#    the ULog, F-RAM and console of A must be identical to the published
#    run, on any host.
#
# Needs python3 with numpy and pyulog (pymavlink for C, matplotlib for the
# charts); on Linux mtools (mcopy) to read the SD card image. Work files:
# examples/px4-sih/verify/work.
# Exit status: 0 ALL PASS; 1 SOME CHECKS FAILED; 2 PARTIAL PASS (A and B
# passed, C skipped for want of pymavlink).
set -e
if [ $# -lt 1 ]; then
	echo "usage: $0 <px4_fmu-v5_default.elf> [athrill3]" >&2
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
# ULog, F-RAM and console of flight A. (The whole SD card image is not
# compared across hosts: formatting it gives a new volume ID each time.)
PUBLISHED_ELF=f8a78a13f3a545305cce45eeef677e4d436325ee6a6065b1753c387283f3d05e
PUBLISHED_ULOG=5aaa66ebf76d7fde713ef39cb07890dc6cb5de136713fd35732121efef116e3d
PUBLISHED_FRAM=c1a69ccb3740c797db62233deca689741a76a20692e982acea95c76199ff50e9
PUBLISHED_CONSOLE=36a05910bf2ca4183818bc822bcefa0d16ebb6382fb29bc5b72cccbf223825ce

rm -rf "$WORK"
mkdir -p "$WORK/golden"
cd "$WORK/golden"
cp "$EXAMPLE/memory.txt" "$EXAMPLE/first-boot-config.txt" "$EXAMPLE/first-boot.nsh" .
cp "$EXAMPLE/make-sd-image.sh" . && ./make-sd-image.sh > /dev/null
echo "golden images: first boot (SYS_AUTOSTART 1100)"
"$ATHRILL" -c1 -m memory.txt -d first-boot-config.txt -t 4000000000 "$ELF" > first-boot.log 2>&1 || true
grep -q "SYS_AUTOSTART: curr: 0 -> new: 1100" first-boot.log

echo "A. determinism: 2 runs fast, 1 run strict, in parallel"
for run in fast1 fast2 strict; do
	mkdir -p "$WORK/$run"
	cd "$WORK/$run"
	cp "$EXAMPLE/memory.txt" "$HERE/scenario.nsh" .
	grep -v UART2_TCP "$EXAMPLE/device_config.txt" > config.txt
	echo "DEVICE_CONFIG_STM32_UART_RX_FILE scenario.nsh" >> config.txt
	if [ $run = strict ]; then
		echo "DEVICE_CONFIG_ARM_FP_MODE strict" >> config.txt
		echo "DEVICE_CONFIG_ARM_EXEC_MODE strict" >> config.txt
	fi
	clone "$WORK/golden/fram.bin" fram.bin
	clone "$WORK/golden/sd.img" sd.img
	("$ATHRILL" -c1 -m memory.txt -d config.txt -t 16000000000 "$ELF" > console.txt 2> stderr.txt || true) &
done
wait
cd "$WORK"
for run in fast1 fast2 strict; do
	if ! grep -aq "EXIT for timeout" $run/console.txt; then
		echo "  [FAIL] $run did not run to its clock limit"
		FAILED=1
	fi
done
for run in fast2 strict; do
	grep -av "DEVICE_CONFIG_ARM_" fast1/console.txt > c1.txt
	grep -av "DEVICE_CONFIG_ARM_" $run/console.txt > c2.txt
	if cmp -s c1.txt c2.txt && cmp -s fast1/fram.bin $run/fram.bin && cmp -s fast1/sd.img $run/sd.img; then
		echo "  [PASS] fast1 = $run: F-RAM, SD card image and console (but the mode lines) identical ($(grep -a 'steps=' $run/stderr.txt | sed 's/.*steps=//') instructions)"
	else
		echo "  [FAIL] fast1 != $run"
		FAILED=1
	fi
done

echo "B. flight log of A"
ulog_of fast1/sd.img a.ulg
python3 "$HERE/analyze_ulog.py" a.ulg || FAILED=1

if python3 -c "import pymavlink" 2> /dev/null; then
	echo "C. MAVLink: take off, go to 5 m north, hover, land"
	mkdir -p "$WORK/mavlink"
	cd "$WORK/mavlink"
	cp "$EXAMPLE/memory.txt" "$EXAMPLE/device_config.txt" .
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
if [ "$ELF_SHA" = "$PUBLISHED_ELF" ]; then
	echo "D. the same as the published run (release ELF)"
	grep -av "DEVICE_CONFIG_ARM_" fast1/console.txt > c1.txt
	if [ "$(sha256 a.ulg)" = $PUBLISHED_ULOG ] && [ "$(sha256 fast1/fram.bin)" = $PUBLISHED_FRAM ] && [ "$(sha256 c1.txt)" = $PUBLISHED_CONSOLE ]; then
		echo "  [PASS] ULog, F-RAM and console identical to the published run (ULog sha256 ${PUBLISHED_ULOG%"${PUBLISHED_ULOG#????????????????}"}...)"
	else
		echo "  [FAIL] differs from the published run: ULog $(sha256 a.ulg), F-RAM $(sha256 fast1/fram.bin), console $(sha256 c1.txt)"
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

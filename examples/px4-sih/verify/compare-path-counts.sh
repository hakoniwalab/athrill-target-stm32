#!/bin/sh
# Short SIH regression: boot airframe 1100 once, then run the legacy C board
# path and the Devicetree path from identical images.  1.5 G clocks crosses
# the GPIO-read/WFI sequence that exposed an extra DT batch boundary.
#
#   compare-path-counts.sh <px4_fmu-v5_default.elf> <pixhawk4.dtb> [athrill3]
set -e
if [ $# -lt 2 ]; then
	echo "usage: $0 <px4_fmu-v5_default.elf> <pixhawk4.dtb> [athrill3]" >&2
	exit 1
fi
ELF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
DTB=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
HERE=$(cd "$(dirname "$0")" && pwd)
EXAMPLE=$(dirname "$HERE")
ATHRILL=${3:-$EXAMPLE/../../build/athrill3}
ATHRILL=$(cd "$(dirname "$ATHRILL")" && pwd)/$(basename "$ATHRILL")
WORK=$HERE/count-work
TICKS=1500000000
FAILED=0

case "$DTB" in *' '*)
	echo "pixhawk4.dtb path cannot contain spaces: $DTB" >&2
	exit 1;;
esac

clone() {
	cp -c "$1" "$2" 2>/dev/null || cp --reflink=auto "$1" "$2" 2>/dev/null || cp "$1" "$2"
}

normalize_console() {
	sed -e '/^DEVICE_CONFIG_DEVICE_TREE = /d' \
	    -e '/^DEVICE_TREE sha256=/d' "$1"
}

count_of() {
	sed -n 's/.*SAMPLES halted=[0-9][0-9]* steps=\([0-9][0-9]*\).*/\1/p' "$1" | tail -1
}

rm -rf "$WORK"
mkdir -p "$WORK/golden"
cd "$WORK/golden"
cp "$EXAMPLE/memory.txt" "$EXAMPLE/first-boot-config.txt" \
	"$EXAMPLE/first-boot.nsh" "$EXAMPLE/make-sd-image.sh" .
./make-sd-image.sh > /dev/null
echo "golden images: first boot (SYS_AUTOSTART 1100)"
"$ATHRILL" -c1 -m memory.txt -d first-boot-config.txt -t 4000000000 \
	"$ELF" > first-boot.log 2>&1 || true
grep -q "SYS_AUTOSTART: curr: 0 -> new: 1100" first-boot.log

echo "short SIH: C and DT paths in parallel"
for mode in c dt; do
	mkdir -p "$WORK/$mode"
	cd "$WORK/$mode"
	cp "$EXAMPLE/memory.txt" "$HERE/scenario.nsh" .
	grep -v UART2_TCP "$EXAMPLE/device_config.txt" > config.txt
	echo "DEVICE_CONFIG_STM32_UART_RX_FILE scenario.nsh" >> config.txt
	if [ "$mode" = dt ]; then
		echo "DEVICE_CONFIG_DEVICE_TREE $DTB" >> config.txt
	fi
	clone "$WORK/golden/fram.bin" fram.bin
	clone "$WORK/golden/sd.img" sd.img
	("$ATHRILL" -c1 -m memory.txt -d config.txt -t "$TICKS" "$ELF" \
		> console.txt 2> stderr.txt || true) &
done
wait

cd "$WORK"
for mode in c dt; do
	if ! grep -aq "EXIT for timeout" "$mode/console.txt"; then
		echo "  [FAIL] $mode did not run to its clock limit"
		FAILED=1
	fi
done
C_COUNT=$(count_of c/stderr.txt)
DT_COUNT=$(count_of dt/stderr.txt)
if [ -n "$C_COUNT" ] && [ "$C_COUNT" = "$DT_COUNT" ]; then
	echo "  [PASS] instruction counts identical ($C_COUNT)"
else
	echo "  [FAIL] instruction counts differ: C=${C_COUNT:-missing}, DT=${DT_COUNT:-missing}"
	FAILED=1
fi
normalize_console c/console.txt > c.console
normalize_console dt/console.txt > dt.console
if cmp -s c.console dt.console && cmp -s c/fram.bin dt/fram.bin && \
	cmp -s c/sd.img dt/sd.img; then
	echo "  [PASS] console, F-RAM and SD card image identical"
else
	echo "  [FAIL] C and DT outputs differ"
	FAILED=1
fi
if [ "$FAILED" = 0 ]; then
	echo "ALL PASS"
else
	echo "SOME CHECKS FAILED"
fi
exit "$FAILED"

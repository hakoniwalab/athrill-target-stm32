#!/bin/sh
# Normal-parameter-storage and MOTOR_TEST gate for ArduCopter on Pixhawk 6C.
set -eu

if [ $# -lt 1 ]; then
	echo "usage: $0 <arducopter.elf> [athrill3]" >&2
	exit 2
fi

DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$DIR/../.." && pwd)
ELF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
ATHRILL=${2:-$ROOT/build/athrill3}
ATHRILL=$(cd "$(dirname "$ATHRILL")" && pwd)/$(basename "$ATHRILL")
PYTHON=${PYTHON:-python3}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/athrill-ap-v6c-stage4.XXXXXX")
trap 'rm -r "$WORK"' EXIT HUP INT TERM

echo "ArduCopter first boot: write frame, output and marker parameters to F-RAM"
mkdir -p "$WORK/golden"
cp "$DIR/memory.txt" "$DIR/stage4-device-config.txt" "$ROOT/examples/ardupilot-bench/fram-brd-io-disabled.bin" \
	"$WORK/golden/"
mv "$WORK/golden/fram-brd-io-disabled.bin" "$WORK/golden/fram.bin"
"$PYTHON" "$DIR/make-stage4-request.py" "$WORK/golden/mavlink-request.raw" write
cd "$WORK/golden"
"$ATHRILL" -c1 -m memory.txt -d stage4-device-config.txt -t 16000000000 "$ELF" \
	>write.log 2>&1 || true
grep -aq "EXIT for timeout" write.log
"$PYTHON" "$DIR/check-stage4.py" mavlink.raw --write

echo "ArduCopter second boot: persisted marker and board-order MOTOR_TEST (twice)"
for run in run1 run2; do
	mkdir -p "$WORK/$run"
	cp "$DIR/memory.txt" "$DIR/stage4-device-config.txt" "$WORK/$run/"
	cp "$WORK/golden/fram.bin" "$WORK/$run/fram.bin"
	"$PYTHON" "$DIR/make-stage4-request.py" "$WORK/$run/mavlink-request.raw" bench
	(
		cd "$WORK/$run"
		"$ATHRILL" -c1 -m memory.txt -d stage4-device-config.txt -t 16000000000 "$ELF" \
			>bench.log 2>&1 || true
	) &
done
wait

for run in run1 run2; do
	"$PYTHON" "$DIR/check-stage4.py" "$WORK/$run/mavlink.raw" >"$WORK/$run/summary.txt"
	python3 "$ROOT/tools/check_actuator_trace.py" "$WORK/$run/actuators.txt" \
		1:1:1:916 4:1:4:1000 2:1:2:1083 3:1:3:1166
done

cmp "$WORK/run1/summary.txt" "$WORK/run2/summary.txt"
cmp "$WORK/run1/mavlink.raw" "$WORK/run2/mavlink.raw"
cmp "$WORK/run1/actuators.txt" "$WORK/run2/actuators.txt"
cmp "$WORK/run1/fram.bin" "$WORK/run2/fram.bin"
cat "$WORK/run1/summary.txt"
echo "PASS: ArduCopter motor sequence 1,4,2,3 reached FMU outputs 1,4,2,3"
echo "PASS: ArduCopter stage-4 bench artifacts identical twice"

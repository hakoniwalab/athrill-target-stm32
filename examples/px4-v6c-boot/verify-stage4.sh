#!/bin/sh
# SD/F-RAM/PWM gate for PX4 on Pixhawk 6C, including a repeated bench run.
set -eu

if [ $# -lt 1 ]; then
	echo "usage: $0 <px4_fmu-v6c_default.elf> [athrill3]" >&2
	exit 2
fi

DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$DIR/../.." && pwd)
ELF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
ATHRILL=${2:-$ROOT/build/athrill3}
ATHRILL=$(cd "$(dirname "$ATHRILL")" && pwd)/$(basename "$ATHRILL")
WORK=$(mktemp -d "${TMPDIR:-/tmp}/athrill-px4-v6c-stage4.XXXXXX")
trap 'rm -r "$WORK"' EXIT HUP INT TERM

clone()
{
	cp -c "$1" "$2" 2>/dev/null || cp --reflink=auto "$1" "$2" 2>/dev/null || cp "$1" "$2"
}

mkdir -p "$WORK/golden"
cp "$DIR/memory.txt" "$DIR/stage4-first-boot-config.txt" "$DIR/stage4-first-boot.nsh" "$WORK/golden/"
"$ROOT/examples/px4-sih/make-sd-image.sh" "$WORK/golden/sd.img" >/dev/null
python3 "$ROOT/examples/px4-hakoniwa/put-sd-file.py" "$WORK/golden/sd.img" \
	"$ROOT/docs/px4/x500-hakoniwa-params.sh" X500.SH >/dev/null

echo "PX4 first boot: X500 parameters from SD to normal F-RAM storage"
cd "$WORK/golden"
"$ATHRILL" -c1 -m memory.txt -d stage4-first-boot-config.txt -t 100000000000 "$ELF" \
	>first-boot.log 2>&1 || true
grep -aq "SYS_AUTOSTART: curr: 0 -> new: 4019" first-boot.log
grep -aq "PWM_AUX_FUNC4: curr: 0 -> new: 104" first-boot.log
grep -aq "EXIT for timeout" first-boot.log

echo "PX4 second boot: persisted parameters, ULog and ordered PWM pulses (twice)"
for run in run1 run2; do
	mkdir -p "$WORK/$run"
	cp "$DIR/memory.txt" "$DIR/stage4-bench-config.txt" "$DIR/stage4-bench.nsh" "$WORK/$run/"
	clone "$WORK/golden/fram.bin" "$WORK/$run/fram.bin"
	clone "$WORK/golden/sd.img" "$WORK/$run/sd.img"
	(
		cd "$WORK/$run"
		"$ATHRILL" -c1 -m memory.txt -d stage4-bench-config.txt -t 24000000000 "$ELF" \
			>bench.log 2>&1 || true
	) &
done
wait

for run in run1 run2; do
	log="$WORK/$run/bench.log"
	trace="$WORK/$run/stage4-actuators.txt"
	grep -aq "Loading airframe: .*4019_x500_v2" "$log"
	grep -aq "SYS_AUTOSTART.*4019" "$log"
	grep -aq "Armed by internal command" "$log"
	grep -aq "Disarmed by internal command" "$log"
	grep -aq "/fs/microsd/log/.*\.ulg" "$log"
	python3 "$ROOT/tools/check_actuator_trace.py" "$trace" \
		1:1:1:1180 2:1:2:1260 3:1:3:1340 4:1:4:1420
	python3 - "$WORK/$run/sd.img" <<'PY'
import mmap
import sys

with open(sys.argv[1], "rb") as source:
    with mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as image:
        if image.find(b"ULog\x01\x12\x35") < 0:
            raise SystemExit("ULog header absent from SD image")
PY
done

cmp "$WORK/run1/bench.log" "$WORK/run2/bench.log"
cmp "$WORK/run1/stage4-console.raw" "$WORK/run2/stage4-console.raw"
cmp "$WORK/run1/stage4-actuators.txt" "$WORK/run2/stage4-actuators.txt"
cmp "$WORK/run1/fram.bin" "$WORK/run2/fram.bin"
cmp "$WORK/run1/sd.img" "$WORK/run2/sd.img"

echo "PASS: PX4 v6C SD mounted, X500 parameters persisted, ULog written"
echo "PASS: FMU1..4 TIM1_CH1..4 = 1180,1260,1340,1420 us"
echo "PASS: PX4 stage-4 bench artifacts identical twice"

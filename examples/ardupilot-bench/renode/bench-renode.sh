#!/bin/sh
# Three RTF runs of ArduCopter under Renode with one IMU (as Athrill's board) and with
# ArduPilot's default three IMUs, at ArduPilot's default 462 MIPS.
# Usage: bench-renode.sh <ardupilot-checkout> <renode> <arducopter.elf> [state-dir]
#   ardupilot-checkout: ArduPilot with Tools/renode (run.py)
#   renode: the renode launcher of ArduPilot's performance branch
# Every run starts from this example's F-RAM seed (BRD_IO_ENABLE=0).
# Needs python3 with pymavlink, and what Tools/renode/run.py needs.
set -e
if [ $# -lt 3 ]; then
	echo "usage: $0 <ardupilot-checkout> <renode> <arducopter.elf> [state-dir]" >&2
	exit 1
fi
HERE=$(cd "$(dirname "$0")" && pwd)
AP=$(cd "$1" && pwd)
RENODE=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
ELF=$(cd "$(dirname "$3")" && pwd)/$(basename "$3")
STATE=${4:-$HERE/state}
mkdir -p "$STATE"
STATE=$(cd "$STATE" && pwd)

# Renode's monitor quits at the end of its input: keep stdin open with a FIFO.
rm -f "$STATE/stdin"
mkfifo "$STATE/stdin"
exec 3<>"$STATE/stdin"

for IMUS in 1 3; do
	for RUN in 1 2 3; do
		cp "$HERE/../fram-brd-io-disabled.bin" "$STATE/fram.img"
		(cd "$AP" && python3 Tools/renode/run.py Pixhawk4 --elf "$ELF" \
			--renode "$RENODE" --unthrottled --no-xterm --state-dir "$STATE" --num-imus $IMUS \
			--exec "cpu PerformanceInMips 462" <&3 > "$STATE/renode-imus$IMUS-$RUN.log" 2>&1) &
		python3 "$HERE/measure-rtf.py" "renode ${IMUS} IMU run$RUN" || true
		pkill -f 'Tools/renode/run.py' || true
		pkill -f 'Renode.dll' || true
		sleep 2
	done
done
exec 3>&-
rm -f "$STATE/stdin"

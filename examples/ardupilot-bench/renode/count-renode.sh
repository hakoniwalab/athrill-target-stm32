#!/bin/sh
# Count Renode's device accesses, interrupts and instructions over virtual 20.5-21.5 s of ArduCopter, one IMU.
# Usage: count-renode.sh <ardupilot-checkout> <renode> <arducopter.elf> [state-dir]
# Renode's machine profiler is turned on at virtual 20 s; the run stops at 22 s.
# The profile of 2 virtual seconds is about 1.2 GB. Writes <state-dir>/pages.txt (accesses per 256-byte page).
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

rm -f "$STATE/stdin" "$STATE/profile.bin"
mkfifo "$STATE/stdin"
exec 3<>"$STATE/stdin"
cp "$HERE/../fram-brd-io-disabled.bin" "$STATE/fram.img"
(cd "$AP" && python3 Tools/renode/run.py Pixhawk4 --elf "$ELF" \
	--renode "$RENODE" --unthrottled --no-xterm --state-dir "$STATE" --num-imus 1 \
	--exec "cpu PerformanceInMips 462" <&3 > "$STATE/renode-count.log" 2>&1) &
python3 "$HERE/profiler-window.py" "$STATE/stdin" "$STATE/profile.bin" || true
sleep 3
pkill -f 'Tools/renode/run.py' || true
pkill -f 'Renode.dll' || true
sleep 2
exec 3>&-
rm -f "$STATE/stdin"
python3 "$HERE/profiler-count.py" "$STATE/profile.bin" --pages "$STATE/pages.txt"

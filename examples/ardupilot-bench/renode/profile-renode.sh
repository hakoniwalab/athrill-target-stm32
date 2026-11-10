#!/bin/sh
# Sample Renode's emulation thread for 30 s in the steady state of ArduCopter, one IMU, and print where its time goes.
# Usage: profile-renode.sh <ardupilot-checkout> <renode> <arducopter.elf> [state-dir]
# Uses macOS `sample`. DOTNET_PerfMapEnabled=1 makes .NET write /tmp/perf-<pid>.map, which names its JIT-compiled methods.
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

rm -f "$STATE/stdin"
mkfifo "$STATE/stdin"
exec 3<>"$STATE/stdin"
cp "$HERE/../fram-brd-io-disabled.bin" "$STATE/fram.img"
(cd "$AP" && DOTNET_PerfMapEnabled=1 python3 Tools/renode/run.py Pixhawk4 --elf "$ELF" \
	--renode "$RENODE" --unthrottled --no-xterm --state-dir "$STATE" --num-imus 1 \
	--exec "cpu PerformanceInMips 462" <&3 > "$STATE/renode-profile.log" 2>&1) &
python3 "$HERE/measure-rtf.py" "renode profiled" > "$STATE/rtf.txt" &
MEASURE=$!
PID=
while [ -z "$PID" ]; do sleep 1; PID=$(pgrep -f 'Renode.dll' | head -1); done
sleep 60
sample "$PID" 30 -file "$STATE/renode.sample.txt" > /dev/null 2>&1
cp "/tmp/perf-$PID.map" "$STATE/renode.perfmap"
wait $MEASURE || true
pkill -f 'Tools/renode/run.py' || true
pkill -f 'Renode.dll' || true
exec 3>&-
rm -f "$STATE/stdin"
cat "$STATE/rtf.txt"
python3 "$HERE/sample-share.py" "$STATE/renode.sample.txt" 'ardupilot.cpu[0]' --perfmap "$STATE/renode.perfmap" \
	--group 'device models=Peripherals\.(?!CPU)' --group 'setjmp=setjmp' --group 'time framework=Antmicro\.Renode\.Time\.' \
	--group 'garbage collection=gc_heap|GarbageCollect' --group 'TB invalidation=InvalidateMemoryFragment|InvalidateTranslationBlocks'

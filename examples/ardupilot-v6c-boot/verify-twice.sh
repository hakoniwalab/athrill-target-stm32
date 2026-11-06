#!/bin/sh
# Run the ArduCopter v6C sensor/MAVLink gate twice and compare summaries.
set -eu

if [ $# -lt 1 ]; then
	echo "usage: $0 <arducopter.elf> [athrill3]" >&2
	exit 2
fi

DIR=$(cd "$(dirname "$0")" && pwd)
ELF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
ATHRILL=${2:-$DIR/../../build/athrill3}
PYTHON=${PYTHON:-python3}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/athrill-ap-v6c.XXXXXX")
trap 'rm -r "$TMP"' EXIT HUP INT TERM

"$PYTHON" "$DIR/../ardupilot-bench/make-mavlink-request.py" "$DIR/mavlink-request.raw" --parameters

run_once()
{
	set +e
	"$DIR/run.sh" "$ELF" 4000000000 "$ATHRILL" >"$TMP/emulator-$1.out" 2>"$TMP/emulator-$1.err"
	status=$?
	set -e
	[ "$status" -eq 1 ] || { echo "FAIL: emulator exit $status" >&2; exit 1; }
	"$PYTHON" "$DIR/check-sensors.py" "$DIR/mavlink.raw" >"$TMP/summary-$1.txt"
}

run_once 1
run_once 2
cmp "$TMP/summary-1.txt" "$TMP/summary-2.txt"
cat "$TMP/summary-2.txt"
echo "PASS: identical normalized output twice"
if command -v sha256sum >/dev/null 2>&1; then
	sha256sum "$TMP/summary-1.txt"
else
	shasum -a 256 "$TMP/summary-1.txt"
fi

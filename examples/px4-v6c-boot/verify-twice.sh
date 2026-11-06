#!/bin/sh
# Run the PX4 v6C live-sensor gate twice and compare normalized summaries.
set -eu

if [ $# -lt 1 ]; then
	echo "usage: $0 <px4_fmu-v6c_default.elf> [athrill3]" >&2
	exit 2
fi

DIR=$(cd "$(dirname "$0")" && pwd)
ELF=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
ATHRILL=${2:-$DIR/../../build/athrill3}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/athrill-px4-v6c.XXXXXX")
trap 'rm -r "$TMP"' EXIT HUP INT TERM

run_once()
{
	set +e
	"$DIR/run.sh" "$ELF" 10000000000 "$ATHRILL" >"$TMP/emulator-$1.out" 2>"$TMP/emulator-$1.err"
	status=$?
	set -e
	[ "$status" -eq 1 ] || { echo "FAIL: emulator exit $status" >&2; exit 1; }
	"$DIR/check-sensors.py" "$DIR/console.raw" >"$TMP/summary-$1.txt"
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

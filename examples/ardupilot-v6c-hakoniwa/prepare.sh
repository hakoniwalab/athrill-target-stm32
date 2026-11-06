#!/bin/sh
# Persist the shared ArduCopter x500 parameter set in the v6C F-RAM image.
set -eu

if [ $# -ne 2 ]; then
	echo "usage: $0 <arducopter.elf> <athrill3>" >&2
	exit 2
fi

absolute()
{
	printf '%s/%s\n' "$(cd "$(dirname "$1")" && pwd)" "$(basename "$1")"
}

ELF=$(absolute "$1")
ATHRILL=$(absolute "$2")
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$DIR/../.." && pwd)
PYTHON=${PYTHON:-python3}
cd "$DIR"

if [ -e fram.bin ]; then
	exit 0
fi

cp "$ROOT/examples/ardupilot-hakoniwa/fram-seed.bin" fram.bin.new
echo "first boot: shared ArduCopter x500 parameters into fram.bin"
"$ATHRILL" -c1 -m memory.txt -d first-boot-config.txt -t -1 "$ELF" >first-boot.log 2>&1 &
pid=$!
if "$PYTHON" "$ROOT/examples/ardupilot-hakoniwa/load-params.py" \
	"$ROOT/examples/ardupilot-hakoniwa/x500-hakoniwa-ardupilot.parm" >load-params.log 2>&1; then
	kill "$pid" 2>/dev/null || true
	wait "$pid" 2>/dev/null || true
	mv fram.bin.new fram.bin
	tail -1 load-params.log
else
	kill "$pid" 2>/dev/null || true
	wait "$pid" 2>/dev/null || true
	rm -f fram.bin.new
	echo "first boot failed, see $DIR/load-params.log and $DIR/first-boot.log" >&2
	exit 1
fi

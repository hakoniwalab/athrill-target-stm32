#!/bin/sh
# Make the v6C SD image and persist the shared x500 tune plus v6C sensor IDs.
set -eu

if [ $# -ne 2 ]; then
	echo "usage: $0 <px4_fmu-v6c_default.elf> <athrill3>" >&2
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
EXAMPLES=$ROOT/examples
. "$EXAMPLES/boards.sh"
# The first boot runs the board from its Devicetree with the bench sensors.
DTB=${DEVICE_CONFIG_DEVICE_TREE:-$(board_dtb pixhawk6c pixhawk6c)}
cd "$DIR"

if [ ! -e sd.img ]; then
	rm -f sd.img.blank x500-v6c.sh.tmp
	"$ROOT/examples/px4-sih/make-sd-image.sh" "$DIR/sd.img.blank" >/dev/null
	{
		cat "$ROOT/docs/px4/x500-hakoniwa-params.sh"
		cat "$DIR/x500-v6c-params.sh"
	} >x500-v6c.sh.tmp
	python3 "$ROOT/examples/px4-hakoniwa/put-sd-file.py" sd.img.blank x500-v6c.sh.tmp X500.SH >/dev/null
	mv sd.img.blank sd.img
	rm -f x500-v6c.sh.tmp
fi

if [ ! -e fram.bin ]; then
	echo "first boot: shared x500 parameters and v6C sensor IDs into fram.bin"
	{ cat first-boot-config.txt; printf 'DEVICE_CONFIG_DEVICE_TREE %s\n' "$DTB"; } >first-boot-config.run
	"$ATHRILL" -c1 -m memory.txt -d first-boot-config.run -t 100000000000 "$ELF" >first-boot.log 2>&1 || true
	if ! grep -aq "SYS_AUTOSTART: curr: 0 -> new: 4019" first-boot.log \
		|| ! grep -aq "CAL_ACC0_ID: curr: .* -> new: 4259850" first-boot.log; then
		echo "first boot failed, see $DIR/first-boot.log" >&2
		rm -f fram.bin
		exit 1
	fi
fi

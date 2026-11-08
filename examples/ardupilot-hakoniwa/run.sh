#!/bin/sh
# The official ArduCopter Pixhawk4 firmware on the emulated Pixhawk 4, flying the Hakoniwa Drone x500
# (docs/hakoniwa.md, "ArduPilot"):
#   examples/ardupilot-hakoniwa/run.sh <arducopter.elf> <athrill3> <libhako_sensor_device>
# The first run starts from fram-seed.bin (BRD_IO_ENABLE=0) and an empty SD card, boots ArduCopter on the test
# vehicle (no Hakoniwa), writes x500-hakoniwa-ardupilot.parm over MAVLink (load-params.py, needs pymavlink) and
# stops: the parameters stay in the emulated F-RAM, fram.bin. Every run then boots ArduCopter on Hakoniwa, with
# MAVLink on TCP 127.0.0.1:5760. Start Hakoniwa Drone's aircraft service first, and `hako-cmd start` after this.
# Stop with Ctrl+C. Start again from scratch: rm fram.bin sd.img
set -e
if [ $# -lt 3 ]; then
	echo "usage: $0 <arducopter.elf> <athrill3> <libhako_sensor_device>" >&2
	exit 1
fi
abs() { echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"; }
ELF=$(abs "$1")
ATHRILL=$(abs "$2")
DEVICE=$(abs "$3")
DIR=$(cd "$(dirname "$0")" && pwd)
cd "$DIR"
EXAMPLES=$(cd "$DIR/.." && pwd)
. "$EXAMPLES/boards.sh"
DTB=$(board_dtb pixhawk4 pixhawk4-hakoniwa)
if [ ! -f "$DTB" ]; then
	echo "no $DTB: clone hakoniwalab/athrill-device next to this repository or set ATHRILL_DEVICE_DIR" >&2
	exit 1
fi
# The board from its Devicetree, with the Hakoniwa sensor device as its plugin.
dt_config() {	# configuration -> configuration for this run
	{ grep -v -e '^DEVICE_CONFIG_DEVICE_TREE ' -e '^DEVICE_CONFIG_DEVICE_PLUGIN_0 ' "$1"
	  echo "DEVICE_CONFIG_DEVICE_TREE $DTB"
	  echo "DEVICE_CONFIG_DEVICE_PLUGIN_0 hakoniwa%2Csensor-device=$DEVICE"; } > "$2"
}
dt_config device_config.txt device_config-run.txt
if [ ! -e fram.bin ]; then
	rm -f sd.img
	../px4-sih/make-sd-image.sh "$DIR/sd.img" > /dev/null
	cp fram-seed.bin fram.bin.new
	sed 's/^DEVICE_CONFIG_STM32_FRAM_FILE fram.bin$/DEVICE_CONFIG_STM32_FRAM_FILE fram.bin.new/' first-boot-config.txt > first-boot-config.tmp
	dt_config first-boot-config.tmp first-boot-config-run.txt
	echo "first boot: the x500 parameters into fram.bin (about 2 minutes) ..."
	"$ATHRILL" -c1 -m memory.txt -d first-boot-config-run.txt -t -1 "$ELF" > first-boot.log 2>&1 &
	PID=$!
	if python3 load-params.py x500-hakoniwa-ardupilot.parm > load-params.log 2>&1; then
		kill "$PID"
		wait "$PID" 2> /dev/null || true
		mv fram.bin.new fram.bin
		tail -1 load-params.log
	else
		kill "$PID"
		wait "$PID" 2> /dev/null || true
		rm -f fram.bin.new
		echo "first boot failed, see $DIR/load-params.log and $DIR/first-boot.log" >&2
		exit 1
	fi
fi
echo "ArduCopter on Athrill with Hakoniwa; run hako-cmd start, then connect to TCP 127.0.0.1:5760"
exec "$ATHRILL" -c1 -m memory.txt -d device_config-run.txt -t -1 "$ELF"

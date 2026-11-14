#!/bin/sh
# The ArduPilot bench as a check: ArduCopter Pixhawk4 boots on the emulated Pixhawk 4 for 20 virtual s
# (run-full.sh: bench sensors, MAVLink requests on SERIAL1) and check-bench.py judges its MAVLink output:
# heartbeats, start-up milestones, healthy IMU, compass and barometer, a 3D GPS fix, MAVLink until the end.
#   examples/ardupilot-bench/check.sh <arducopter.elf> [athrill3]
# Needs pymavlink. Exit status 0 PASS, 1 FAIL.
set -e
if [ $# -lt 1 ]; then
	echo "usage: $0 <arducopter.elf> [athrill3]" >&2
	exit 2
fi
DIR=$(cd "$(dirname "$0")" && pwd)
sh "$DIR/run-full.sh" "$1" 4320000000 ${2:+"$2"} > "$DIR/check.log" 2>&1 || true
python3 "$DIR/check-bench.py" "$DIR/mavlink-full.raw"

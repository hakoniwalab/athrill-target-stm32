#!/bin/sh
# GDB remote debugging of the bare-metal hello world (test/hello):
#   test/gdb/run.sh <athrill3> <gdb> <device_config> <port>
# Starts athrill3 -g<port>, then gdb breaks at next_char, reads and writes
# registers, steps one instruction and reads memory. Expected values come
# from test/hello/start.S, not from Athrill: at the first next_char r0 is
# USART3 (0x40004800), r1 is the message; the ldrb loads 'H' (0x48) into r2;
# xPSR has the Thumb bit (0x01000000). Through the target description
# (toppers/athrill#85): MSP is the current sp in Thread mode after reset,
# BASEPRI and s0 are written and read back.
set -eu
athrill=$1
gdb=$2
config=$3
port=$4
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
# athrill3 keeps the port until it has exited: wait for it, so that the
# next run (ctest runs this test right after others) can listen again
trap 'kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; rm -rf "$work"' EXIT INT TERM
"$athrill" -c1 -g"$port" -m "$here/hello/memory.txt" -d "$config" -t 100000 \
	"$here/hello/hello.elf" > "$work/athrill.log" 2>&1 &
pid=$!
i=0
# the server's line, "gdb server listening on port <port>" (gdb_rsp.c)
while ! grep -q "gdb server listening" "$work/athrill.log" 2>/dev/null && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
if ! grep -q "gdb server listening" "$work/athrill.log" 2>/dev/null; then
	cat "$work/athrill.log"
	echo "FAIL: athrill3 did not start its gdb server on port $port"
	exit 1
fi
cat > "$work/commands" <<EOC
set pagination off
target remote 127.0.0.1:$port
break next_char
continue
printf "R0=%08x\n", \$r0
printf "XPSR=%08x\n", \$xpsr
printf "MSG=%08x\n", \$r1 - (unsigned int)&message
stepi
printf "R2=%08x\n", \$r2
printf "BYTE=%02x\n", *(unsigned char *)&message
set var \$r5 = 0x12345678
printf "R5=%08x\n", \$r5
printf "MSPSP=%d\n", \$msp == \$sp
set var \$basepri = 0x50
printf "BASEPRI=%02x\n", \$basepri
set var \$s0 = 1.5
printf "S0=%f\n", \$s0
delete
detach
EOC
"$gdb" -batch -x "$work/commands" "$here/hello/hello.elf" > "$work/gdb.log" 2>&1 || true
cat "$work/gdb.log"
for expected in "R0=40004800" "XPSR=01000000" "MSG=00000000" "R2=00000048" "BYTE=48" "R5=12345678" \
	"MSPSP=1" "BASEPRI=50" "S0=1.500000"; do
	if ! grep -q "$expected" "$work/gdb.log"; then
		echo "FAIL: $expected"
		exit 1
	fi
done
echo "PASS"

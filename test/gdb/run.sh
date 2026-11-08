#!/bin/sh
# GDB remote debugging of the bare-metal hello world (test/hello):
#   test/gdb/run.sh <athrill3> <gdb> <device_config> <port>
# Starts athrill3 -g<port>, then gdb breaks at next_char, reads and writes
# registers, steps one instruction and reads memory. Expected values come
# from test/hello/start.S, not from Athrill: at the first next_char r0 is
# USART3 (0x40004800), r1 is the message; the ldrb loads 'H' (0x48) into r2;
# xPSR has the Thumb bit (0x01000000).
set -eu
athrill=$1
gdb=$2
config=$3
port=$4
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'kill "$pid" 2>/dev/null || true; rm -rf "$work"' EXIT INT TERM
"$athrill" -c1 -g"$port" -m "$here/hello/memory.txt" -d "$config" -t 100000 \
	"$here/hello/hello.elf" > "$work/athrill.log" 2>&1 &
pid=$!
i=0
while ! grep -q "GDB" "$work/athrill.log" 2>/dev/null && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
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
delete
detach
EOC
"$gdb" -batch -x "$work/commands" "$here/hello/hello.elf" > "$work/gdb.log" 2>&1 || true
cat "$work/gdb.log"
for expected in "R0=40004800" "XPSR=01000000" "MSG=00000000" "R2=00000048" "BYTE=48" "R5=12345678"; do
	if ! grep -q "$expected" "$work/gdb.log"; then
		echo "FAIL: $expected"
		exit 1
	fi
done
echo "PASS"

#!/bin/sh
# Runs a Zephyr application built for Zephyr's nucleo_f767zi or nucleo_h743zi
# board on Athrill, on athrill-device's board of the same name:
#   examples/zephyr/run.sh <nucleo_f767zi|nucleo_h743zi> <zephyr.elf> [virtual seconds] [athrill3] [extra-config]
# extra-config: lines appended to the device configuration (for example
# DEVICE_CONFIG_STM32_UART_TX_INSTANT 1).
# The console (USART3) is Athrill's console. Without virtual seconds it runs
# until Ctrl+C.
set -eu
if [ $# -lt 2 ]; then
	echo "usage: $0 <nucleo_f767zi|nucleo_h743zi> <zephyr.elf> [virtual seconds] [athrill3] [extra-config]" >&2
	exit 1
fi
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/../boards.sh"
ELF=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
SECONDS_LIMIT=${3:-}
ATHRILL=${4:-$HERE/../../build/athrill3}
ATHRILL=$(cd "$(dirname "$ATHRILL")" && pwd)/$(basename "$ATHRILL")
EXTRA=
if [ $# -ge 5 ]; then
	EXTRA=$(cd "$(dirname "$5")" && pwd)/$(basename "$5")
fi
case $1 in
nucleo_f767zi)
	DTB=$(board_dtb nucleo-f767zi nucleo-f767zi); MHZ=216
	MEMORY="ROM, 0x08000000, 2048
RAM, 0x20000000, 512" ;;
nucleo_h743zi)
	DTB=$(board_dtb nucleo-h743zi nucleo-h743zi); MHZ=480
	MEMORY="ROM, 0x08000000, 2048
RAM, 0x00000000, 64
RAM, 0x20000000, 128
RAM, 0x24000000, 512
RAM, 0x30000000, 288
RAM, 0x38000000, 64
RAM, 0x38800000, 4" ;;
*)
	echo "unknown board $1 (nucleo_f767zi or nucleo_h743zi)" >&2
	exit 1 ;;
esac
if [ ! -f "$DTB" ]; then
	echo "no $DTB: clone hakoniwalab/athrill-device next to this repository or set ATHRILL_DEVICE_DIR" >&2
	exit 1
fi
RUN=$HERE/work/run/$1
mkdir -p "$RUN"
cd "$RUN"
printf '%s\n' "$MEMORY" > memory.txt
cat > device_config.txt <<EOF
DEVICE_CPU_FREQ $MHZ
DEBUG_FUNC_ENABLE_SKIP_CLOCK 1
DEVICE_CONFIG_STM32_BOOT_ADDR 0x08000000
DISABLE_SYMBOL_UNDERSCORE 1
DEVICE_CONFIG_STM32_CONSOLE_UART 3
DEVICE_CONFIG_DEVICE_TREE $DTB
EOF
if [ -n "$EXTRA" ]; then
	cat "$EXTRA" >> device_config.txt
fi
if [ -n "$SECONDS_LIMIT" ]; then
	exec "$ATHRILL" -c1 -m memory.txt -d device_config.txt -t $((MHZ * 1000000 * SECONDS_LIMIT)) "$ELF"
fi
exec "$ATHRILL" -c1 -m memory.txt -d device_config.txt -t -1 "$ELF"

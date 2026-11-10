#!/bin/sh
# Runs a Zephyr application built for nucleo_f767zi on Renode's stock STM32F746 platform, for comparison:
#   examples/zephyr/renode/run-renode.sh <renode> <zephyr.elf> <virtual seconds> [work-dir]
# 216 MIPS (Athrill's 216 MHz, one instruction per clock), unthrottled (SetGlobalAdvanceImmediately).
# USART3 goes to <work-dir>/console.txt. Prints the wall time and the executed instructions.
# The STM32F746 and STM32F767 share the peripherals this uses; the F767's larger flash is not needed.
set -eu
if [ $# -lt 3 ]; then
	echo "usage: $0 <renode> <zephyr.elf> <virtual seconds> [work-dir]" >&2
	exit 1
fi
RENODE=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
ELF=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${4:-$HERE/work}
mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)
rm -f "$WORK/console.txt"
cat > "$WORK/run.resc" <<RESC
mach create "f767"
machine LoadPlatformDescription @platforms/cpus/stm32f746.repl
sysbus LoadELF @$ELF
cpu PerformanceInMips 216
emulation SetGlobalAdvanceImmediately true
usart3 CreateFileBackend @$WORK/console.txt true
emulation RunFor "$3"
cpu ExecutedInstructions
q
RESC
/usr/bin/time -p "$RENODE" --disable-gui --console -e "include @$WORK/run.resc" > "$WORK/renode.log" 2> "$WORK/time.txt" < /dev/null
echo "wall $(grep '^real' "$WORK/time.txt" | cut -d' ' -f2) s, instructions $(grep -o '0x[0-9A-F]\{16\}' "$WORK/renode.log" | tail -1)"

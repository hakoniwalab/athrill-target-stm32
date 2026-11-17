#!/bin/sh
# Stage 3 ground truth for the real STM32 IWDG model (athrill-device st,stm32-watchdog):
# the UNMODIFIED NUCLEO-F767ZI board Devicetree (its watchdog node is now the IWDG
# model), a firmware that drives the real IWDG registers (KR/PR/RLR). Without a
# kick it resets exactly once; the second boot verifies that RLR has its reset
# value. Kicking every loop keeps it alive. The bite is run twice and its console
# evidence and exact IWDG_RESET clock must be bit-identical.
# Usage: run-iwdg.sh [athrill3 [athrill-device-dir]]
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ATHRILL=${1:-${ATHRILL:-$HERE/../../build-fast/athrill3}}
[ -x "$ATHRILL" ] || ATHRILL=$HERE/../../build/athrill3
DEV=${2:-${ATHRILL_DEVICE_DIR:-$HERE/../../../athrill-device}}
DTB=$DEV/boards/nucleo-f767zi/nucleo-f767zi.dtb
WORK=${WDT_TEST_WORK_DIR:-$PWD}
BITE_ELF=$HERE/iwdg-bite.elf
CONTROL_ELF=$HERE/iwdg-control.elf
[ -x "$ATHRILL" ] && [ -r "$DTB" ] && [ -r "$BITE_ELF" ] && [ -r "$CONTROL_ELF" ]

mkdir -p "$WORK"
cat > "$WORK/device_config_iwdg.txt" <<CFG
DEVICE_CPU_FREQ 216
DEBUG_FUNC_ENABLE_SKIP_CLOCK 1
DEVICE_CONFIG_STM32_BOOT_ADDR 0x08000000
DISABLE_SYMBOL_UNDERSCORE 1
DEVICE_CONFIG_STM32_CONSOLE_UART 3
DEVICE_CONFIG_DEVICE_TREE $DTB
DEVICE_CONFIG_IWDG_RESET 1
CFG

CLOCKS=10800000
run_case()
{
	name=$1
	elf=$2
	"$ATHRILL" -c1 -m "$HERE/memory.txt" -d "$WORK/device_config_iwdg.txt" -t "$CLOCKS" "$elf" \
		>"$WORK/$name.out" 2>"$WORK/$name.err" || true
	grep -E '^(BOOT|RESET_OK|RESET_BAD|ALIVE)$' "$WORK/$name.out" >"$WORK/$name.console" || true
	grep '^IWDG_RESET:' "$WORK/$name.err" >"$WORK/$name.reset" || true
}

run_case bite-a "$BITE_ELF"
run_case bite-b "$BITE_ELF"
run_case control "$CONTROL_ELF"

bite=$(grep -c '^BOOT$' "$WORK/bite-a.console" || true)
ok=$(grep -c '^RESET_OK$' "$WORK/bite-a.console" || true)
bad=$(grep -c '^RESET_BAD$' "$WORK/bite-a.console" || true)
resets=$(wc -l <"$WORK/bite-a.reset" | tr -d ' ')
ctrl=$(grep -c '^BOOT$' "$WORK/control.console" || true)
alive=$(grep -c '^ALIVE$' "$WORK/control.console" || true)
ctrl_resets=$(wc -l <"$WORK/control.reset" | tr -d ' ')
reset_clock=$(sed -n 's/^IWDG_RESET: underflow cpu_clock=//p' "$WORK/bite-a.reset")
reset_clock=${reset_clock:-0}
coverage_ok=0
if [ "$reset_clock" -gt 0 ] && [ "$CLOCKS" -ge $((reset_clock * 5)) ]; then
	coverage_ok=1
fi

echo "bite (no kick) BOOT count = $bite (expect 2: exactly one reboot)"
echo "bite peripheral reset evidence = $ok OK / $bad BAD"
echo "control (kick) BOOT count = $ctrl, ALIVE count = $alive (run clock $CLOCKS, watchdog timeout $reset_clock)"
[ "$resets" -eq 1 ] && cat "$WORK/bite-a.reset"

[ "$bite" -eq 2 ] && [ "$ok" -eq 1 ] && [ "$bad" -eq 0 ] && [ "$resets" -eq 1 ] && \
	[ "$ctrl" -eq 1 ] && [ "$alive" -ge 5 ] && [ "$ctrl_resets" -eq 0 ] && [ "$coverage_ok" -eq 1 ] && \
	cmp -s "$WORK/bite-a.console" "$WORK/bite-b.console" && \
	cmp -s "$WORK/bite-a.reset" "$WORK/bite-b.reset" && { echo "PASS"; exit 0; }
echo "FAIL"
exit 1

#!/bin/sh
# Stage C ground-truth for the watchdog reset path (athrill-device#138):
# a real NUCLEO-F767ZI board Devicetree, with its watchdog node retargeted to
# the test watchdog model, and a bare firmware that prints "BOOT" on USART3.
#   - without a kick the watchdog times out and resets the MCU: BOOT prints
#     again after the reboot (count >= 2);
#   - kicking the watchdog every loop keeps it alive: BOOT prints once.
# Needs arm-none-eabi-gcc and a built athrill3. athrill-device is found through
# ATHRILL_DEVICE_DIR, else the checkout next to this repository.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE"
ATHRILL=${ATHRILL:-$HERE/../../build-fast/athrill3}
[ -x "$ATHRILL" ] || ATHRILL=$HERE/../../build/athrill3
DEV=${ATHRILL_DEVICE_DIR:-$HERE/../../../athrill-device}
SRC_DTB=$DEV/boards/nucleo-f767zi/nucleo-f767zi.dtb
GCC=${GCC:-arm-none-eabi-gcc}

$GCC -mcpu=cortex-m7 -mthumb -nostdlib -T link.ld -o bite.elf wdt.S
$GCC -mcpu=cortex-m7 -mthumb -nostdlib -DKICK -T link.ld -o control.elf wdt.S

# retarget the board's watchdog node to the test model (equal-length string)
python3 - "$SRC_DTB" stage_c.dtb <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
d = open(src, "rb").read()
old, new = b"st,stm32-watchdog\0", b"athrill,test-wdog\0"
assert len(old) == len(new) and d.count(old) == 1, (len(old), len(new), d.count(old))
open(dst, "wb").write(d.replace(old, new))
PY

cat > device_config.txt <<CFG
DEVICE_CPU_FREQ 216
DEBUG_FUNC_ENABLE_SKIP_CLOCK 1
DEVICE_CONFIG_STM32_BOOT_ADDR 0x08000000
DISABLE_SYMBOL_UNDERSCORE 1
DEVICE_CONFIG_STM32_CONSOLE_UART 3
DEVICE_CONFIG_DEVICE_TREE $HERE/stage_c.dtb
CFG

CLOCKS=10800000	# 0.05 s at 216 MHz; the default 5 ms timeout fires inside it
bite=$("$ATHRILL" -c1 -m memory.txt -d device_config.txt -t $CLOCKS bite.elf 2>/dev/null | grep -c BOOT || true)
ctrl=$("$ATHRILL" -c1 -m memory.txt -d device_config.txt -t $CLOCKS control.elf 2>/dev/null | grep -c BOOT || true)
echo "bite (no kick) BOOT count = $bite (expect 2: exactly one watchdog reset)"
echo "control (kick) BOOT count = $ctrl (expect 1: no reset)"
[ "$bite" -eq 2 ] && [ "$ctrl" -eq 1 ] && { echo "PASS"; exit 0; } || { echo "FAIL"; exit 1; }

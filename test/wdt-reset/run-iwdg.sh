#!/bin/sh
# Stage 3 ground truth for the real STM32 IWDG model (athrill-device st,stm32-watchdog):
# the UNMODIFIED NUCLEO-F767ZI board Devicetree (its watchdog node is now the IWDG
# model), a firmware that drives the real IWDG registers (KR/PR/RLR). Without a kick
# the IWDG resets the MCU and the firmware re-arms each reboot, so BOOT prints many
# times; kicking every loop keeps it alive, so BOOT prints once.
# Needs arm-none-eabi-gcc and a built athrill3; athrill-device via ATHRILL_DEVICE_DIR.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
cd "$HERE"
ATHRILL=${ATHRILL:-$HERE/../../build-fast/athrill3}
[ -x "$ATHRILL" ] || ATHRILL=$HERE/../../build/athrill3
DEV=${ATHRILL_DEVICE_DIR:-$HERE/../../../athrill-device}
DTB=$DEV/boards/nucleo-f767zi/nucleo-f767zi.dtb
GCC=${GCC:-arm-none-eabi-gcc}

$GCC -mcpu=cortex-m7 -mthumb -nostdlib -T link.ld -o iwdg-bite.elf iwdg.S
$GCC -mcpu=cortex-m7 -mthumb -nostdlib -DKICK -T link.ld -o iwdg-control.elf iwdg.S

cat > device_config_iwdg.txt <<CFG
DEVICE_CPU_FREQ 216
DEBUG_FUNC_ENABLE_SKIP_CLOCK 1
DEVICE_CONFIG_STM32_BOOT_ADDR 0x08000000
DISABLE_SYMBOL_UNDERSCORE 1
DEVICE_CONFIG_STM32_CONSOLE_UART 3
DEVICE_CONFIG_DEVICE_TREE $DTB
CFG

CLOCKS=10800000
bite=$("$ATHRILL" -c1 -m memory.txt -d device_config_iwdg.txt -t $CLOCKS iwdg-bite.elf 2>/dev/null | grep -c BOOT || true)
ctrl=$("$ATHRILL" -c1 -m memory.txt -d device_config_iwdg.txt -t $CLOCKS iwdg-control.elf 2>/dev/null | grep -c BOOT || true)
echo "bite (no kick) BOOT count = $bite (expect >= 2: the IWDG reset the MCU, re-armed each reboot)"
echo "control (kick) BOOT count = $ctrl (expect 1: no reset)"
[ "$bite" -ge 2 ] && [ "$ctrl" -eq 1 ] && { echo "PASS"; exit 0; } || { echo "FAIL"; exit 1; }

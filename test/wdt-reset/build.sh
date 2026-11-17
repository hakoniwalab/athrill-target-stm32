#!/bin/sh
# Regenerate the bare-metal watchdog fixtures committed beside this script.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
GCC=${GCC:-arm-none-eabi-gcc}

"$GCC" -mcpu=cortex-m7 -mthumb -nostdlib -T "$HERE/link.ld" -o "$HERE/bite.elf" "$HERE/wdt.S"
"$GCC" -mcpu=cortex-m7 -mthumb -nostdlib -DKICK -T "$HERE/link.ld" -o "$HERE/control.elf" "$HERE/wdt.S"
"$GCC" -mcpu=cortex-m7 -mthumb -nostdlib -T "$HERE/link.ld" -o "$HERE/iwdg-bite.elf" "$HERE/iwdg.S"
"$GCC" -mcpu=cortex-m7 -mthumb -nostdlib -DKICK -T "$HERE/link.ld" -o "$HERE/iwdg-control.elf" "$HERE/iwdg.S"

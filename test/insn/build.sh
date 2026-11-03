#!/bin/sh
# Builds the instruction tests with the toolchain image (docker/Dockerfile)
# and makes their expected output with QEMU:
#   docker build -t athrill-stm32-toolchain docker/
#   test/insn/build.sh
# <test>.elf is for Athrill's STM32 target (Cortex-M7, STM32F7 USART);
# <test>.expected is the output of the same program built for QEMU's
# netduinoplus2 (Cortex-M4, STM32F4 USART), whose Armv7-M integer
# instructions mean the same: a reference independent of Athrill.
set -e
cd "$(dirname "$0")"
tests="insn dp"
docker run --rm -v "$PWD:/work" -w /work athrill-stm32-toolchain sh -c "
  set -e
  for t in $tests; do
    arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -nostdlib -T link.ld -o \$t.elf \$t.S
    arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -nostdlib -DUSART_STM32F4 -T link.ld -o /tmp/\$t-qemu.elf \$t.S
    timeout 10 qemu-system-arm -M netduinoplus2 -kernel /tmp/\$t-qemu.elf -display none \
      -serial null -serial null -serial stdio -monitor none 2>/dev/null | tr -d '\r' | sed '/^end\$/q' > \$t.expected || true
    grep -qx end \$t.expected || { echo \"\$t: QEMU did not reach the end\"; exit 1; }
  done"

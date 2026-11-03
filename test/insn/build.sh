#!/bin/sh
# Builds insn.elf with the toolchain image (docker/Dockerfile):
#   docker build -t athrill-stm32-toolchain docker/
#   test/insn/build.sh
set -e
cd "$(dirname "$0")"
docker run --rm -v "$PWD:/work" -w /work athrill-stm32-toolchain sh -c \
  'arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -nostdlib -T link.ld -o insn.elf insn.S && arm-none-eabi-objdump -d insn.elf > insn.lst'

#!/bin/sh
# Builds hello.elf with the toolchain image (docker/Dockerfile):
#   docker build -t athrill-stm32-toolchain docker/
#   test/hello/build.sh
set -e
cd "$(dirname "$0")"
docker run --rm -v "$PWD:/work" -w /work athrill-stm32-toolchain sh -c \
  'arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -nostdlib -T link.ld -o hello.elf start.S && arm-none-eabi-objdump -d hello.elf > hello.lst'

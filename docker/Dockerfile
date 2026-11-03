# Toolchain for the Athrill STM32 target: PX4's own build image (it carries
# the Arm GNU toolchain, arm-none-eabi-gcc, that PX4 uses for Pixhawk) plus
# the reference tools used by the tests (docs/testing.md): QEMU's Cortex-M7
# machine and llvm-objdump.
FROM px4io/px4-dev:v1.17.0-beta1

RUN apt-get update \
 && apt-get install -y --no-install-recommends qemu-system-arm llvm \
 && rm -rf /var/lib/apt/lists/*

# athrill-target-stm32

The STM32 target for Athrill. Its goal is a lightweight Processor-in-the-Loop
(PIL): PX4 built for a Pixhawk board (first Pixhawk 4, `px4_fmu-v5`,
STM32F765) runs on Athrill and flies a vehicle simulated by Hakoniwa in
lockstep.

See [docs/design.md](docs/design.md) for the design decisions and stages, and
[docs/survey.md](docs/survey.md) for the survey of existing work, and
[docs/testing.md](docs/testing.md) for how the target is tested (toolchain in
[docker/Dockerfile](docker/Dockerfile)).

The RISC-V target ([athrill-target-riscv](https://github.com/hakoniwalab/athrill-target-riscv))
already runs PX4 on the AIFoundry Erbium board this way.

## Status

The base of the target runs a bare-metal hello world: Athrill's core with the
STM32 target, reset from the vector table (main stack pointer and reset
handler), USART3 transmit (ISR, TDR), and Thumb instructions decoded by
decoders generated from Arm's machine-readable architecture data and
cross-checked against GNU objdump and llvm-objdump
([docs/decoder.md](docs/decoder.md)): a table for every 16-bit encoding and
generated code for the 32-bit ones (Cortex-M7, sampled). Instructions are
executed through the Arm pseudo-code shared with Athrill's other Arm targets
([athrill-arm-common](https://github.com/hakoniwalab/athrill-arm-common)),
with this target's M-profile implementation of its profile interface
(`src/cpu/arm_target_profile.h`). The data-processing instructions (with the
SP-relative forms, ADR and MOVT), the loads and stores, the branches (B, BL,
BLX, BX, CBZ, CBNZ, TBB, TBH), the extend, bitfield and bit operations (REV,
RBIT, CLZ) and the multiply and divide instructions run that way in all their
16- and 32-bit encodings; the glue of all but the first data-processing
instructions is generated from a table (`tools/glue_table.py`,
`tools/generate_glue.py`), and every encoding is tested against QEMU
(`test/insn`, `docs/testing.md`). Exceptions, the IT block and the MPU are
not modelled yet.

## Build and run

The Athrill core (`toppers/athrill`, branch `cpp`) and
[athrill-arm-common](https://github.com/hakoniwalab/athrill-arm-common) are
expected next to this checkout as `../athrill` and `../athrill-arm-common`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/athrill3 -c1 -m test/hello/memory.txt -d test/hello/device_config.txt -t 100000 test/hello/hello.elf
```

prints `Hello, Athrill STM32!` and stops after 100,000 clocks.
`(cd build && ctest)` runs the same check, the decoder check, and the
instruction test (`test/insn`, built with `test/insn/build.sh`). `test/hello/hello.elf` is built
with the toolchain image:

```bash
docker build -t athrill-stm32-toolchain docker/
test/hello/build.sh
```

## Third-party notice

`src/cpu/decoder/t32_decode16.{h,c}` and `t32_decode32.{h,c}` are generated from Arm's Architecture
Machine Readable Specification and are provided under Arm's BSD 3-clause
licence, reproduced in those files (Copyright 2010-2026 Arm Limited or its
affiliates). The rest of the repository is under the MIT licence.


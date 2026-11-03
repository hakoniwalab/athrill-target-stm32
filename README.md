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
handler), a bootstrap Thumb decoder for the 16-bit instructions the program
uses, and USART3 transmit (ISR, TDR). The bootstrap decoder is replaced by
the generated one (docs/design.md, decision 10).

## Build and run

The Athrill core (`toppers/athrill`, branch `cpp`) is expected next to this
checkout as `../athrill`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/athrill3 -c1 -m test/hello/memory.txt -d test/hello/device_config.txt -t 100000 test/hello/hello.elf
```

prints `Hello, Athrill STM32!` and stops after 100,000 clocks.
`(cd build && ctest)` runs the same check. `test/hello/hello.elf` is built
with the toolchain image:

```bash
docker build -t athrill-stm32-toolchain docker/
test/hello/build.sh
```


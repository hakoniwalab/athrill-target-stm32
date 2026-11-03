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

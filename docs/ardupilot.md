# ArduPilot on Athrill STM32, and a comparison with Renode

The unmodified ArduCopter firmware for the Pixhawk 4 boots on Athrill STM32,
the same emulated board that runs PX4. ArduPilot also runs this firmware on
[Renode](https://ardupilot.org/dev/docs/renode-emulation.html), so the two
emulators can be compared on the same ELF.

**Results:**
- **Speed.** On the same Apple M2 Pro, ArduPilot's performance-patched Renode
  ran the firmware at a real-time factor (RTF) of **0.30**. Athrill:

  | Athrill configuration | RTF | Against Renode |
  | --- | ---: | ---: |
  | Idle loop executed as written (hardware only, no skip possible) | 0.62 | about 2x |
  | A branch to itself waits as `WFI` (`DEVICE_CONFIG_ARM_SPIN_HALT`, no knowledge of the firmware) | **1.583** | **about 5x** |
  | The OS idle function named (`DEVICE_CONFIG_IDLE_FUNCTION`) | 1.592 | about 5x |

  All three give bit-identical MAVLink output.
- **Instructions.** Both emulators executed the firmware along the same path,
  instruction address for instruction address. The two traces differ only
  where the firmware waits on hardware timing.
- **Caveat.** The emulated peripherals differ, so these are results for these
  two benchmark configurations, not a fully normalised comparison of
  workloads.

This is a first step. See [What comes next](#what-comes-next).

## Why Athrill was faster

The firmware, the host and the workload were held the same; what differs is
how each emulator advances time. In short:

1. **Athrill does not execute idle time.**
   - ArduCopter is idle for 79% of its clocks. Its idle thread is a busy
     loop without `WFI` (`b .`), so an emulator that executes it spends host
     time on nothing.
   - A `b .` can only be left by an interrupt, so waiting in it is waiting
     at `WFI`. With `DEVICE_CONFIG_ARM_SPIN_HALT`, Athrill halts the core
     when it executes a branch to itself, and jumps the clock to the next
     device event. The result does not change.
   - This uses only the meaning of the instruction, not the firmware's
     symbols. Naming the idle function instead gives the same speed.
   - Executing the idle loop as written makes Athrill 2.5 times slower (RTF
     0.62), with identical MAVLink output.
2. **A device access is cheap in Athrill.**
   - Athrill's clock is exact to the instruction, so a device computes its
     state (a timer's counter, a cycle counter, a UART's shift register)
     from the clock at the moment the firmware reads it.
   - The access is a direct function call, without locks or a time
     framework in between.
   - Renode's ArduPilot notes say its cost is in the framework around each
     register access and timer event, not in the CPU emulation (1–3%). They
     also name a cheap virtual-time primitive within a quantum as the
     biggest remaining win.
   - Even with its idle loop executed, Athrill (0.62) is twice as fast as
     Renode (0.30).

**Renode's time does not go into executing instructions.** One would
expect Renode's JIT to win where every instruction is executed. The
measurements say otherwise:

| | Instructions per virtual second | RTF | Instructions per wall second |
| --- | ---: | ---: | ---: |
| Athrill, idle loop executed (interpreter) | 216 million | 0.62 | 133 million |
| Renode, 216 MIPS (JIT) | 216 million | 0.306 | 66 million |
| Renode, 462 MIPS (JIT) | 462 million | 0.299 | 138 million |

- **Twice the instructions, the same speed.** At 462 MIPS, Renode executes
  more than twice as many instructions per virtual second as at 216 MIPS,
  and its RTF does not change.
- **So instructions are nearly free in Renode, and its time goes into
  advancing virtual time:** timers, events and register accesses.
- **This matches ArduPilot's profile of Renode,** where the CPU emulation is
  1–3% of the cost.
- **The JIT does win at executing instructions,** but that is not where the
  time goes.
- **Athrill's interpreter executes instructions more slowly,** but advancing
  its time and its devices costs little, so it is faster overall.

**Is the idle setting fair?**
- Renode skips time at `WFI` as well, but this firmware contains no `WFI` or
  `WFE` at all: its idle thread is `b .`. Left to the firmware, neither
  emulator would ever skip time.
- The comparison uses `DEVICE_CONFIG_ARM_SPIN_HALT`: a general rule about
  the instruction set, not knowledge of this firmware. It is off by default.
- Renode has no such rule today. A hook on the idle thread could give it one;
  we have not tried.
- With the idle loop executed as written, Athrill is still twice as fast
  (see point 2).

**What is measured and what is not:**
- The first part is measured within Athrill.
- The second is our reading of Renode's own published profile. Renode was not
  profiled in this comparison.

[docs/time-skip.md](time-skip.md) describes the design, the measurements and
the sources in full.

## The firmware

| | |
| --- | --- |
| Firmware | ArduCopter V4.7.2, `Pixhawk4`, from [firmware.ardupilot.org](https://firmware.ardupilot.org/Copter/stable/Pixhawk4/) (`arducopter.elf`, git `fb773567`) |
| SHA-256 | `cfa1b3f04f326ab91760c53b93503b669d4d62e2d43dd1ffc4d09cb15198eb2c` |
| Changes | none |

**What Athrill needed for ArduPilot, beyond PX4:**
- **The DWT cycle counter.** ChibiOS reads `DWT_CYCCNT` at start-up. It now
  counts Athrill's virtual clock.
- **UART transmit DMA.** ArduPilot sends MAVLink with DMA. This is off by
  default, so the PX4 runs and their published hashes stay as they were.
- **A board without the IO coprocessor.**
  - The Pixhawk 4's IOMCU is not emulated. Without it, ArduPilot fails to
    update the IO firmware and stays in its configuration-error loop.
  - The example's F-RAM seed therefore stores `BRD_IO_ENABLE=0`, the normal
    ArduPilot setting for a board used without an IOMCU.
  - The seed is parameter data that ArduPilot itself wrote. It contains no
    ArduPilot code.

No ArduPilot, ChibiOS or Renode code is in this repository.

## Speed

### Conditions

- **Host:** see [Test machine](#test-machine).
- **Firmware:** the same ELF in both emulators.
- **F-RAM:** both emulators start every run from the same seed with
  `BRD_IO_ENABLE=0`.
- **Ground station:**
  - both emulators receive a heartbeat and a request for all of ArduPilot's
    data streams at 2 Hz, every 2 s;
  - so both firmwares do the same telemetry work.
- **Measured interval:** virtual time 20 s to 80 s, after the boot.
- **RTF:** virtual seconds divided by wall seconds.

| | Run 1 | Run 2 | Run 3 | Mean |
| --- | ---: | ---: | ---: | ---: |
| **Athrill STM32**, branch to itself as `WFI` (the example's setting) | 1.585 | 1.578 | 1.585 | **1.583** |
| Athrill STM32, the idle function named | 1.596 | 1.579 | 1.600 | 1.592 |
| Renode, ArduPilot's performance branch, 462 MIPS (ArduPilot's default) | 0.299 | 0.301 | 0.301 | 0.300 |
| Renode, the same, 216 MIPS (Athrill's setting) | 0.309 | 0.305 | 0.306 | 0.307 |

Athrill: 216 MHz, the clock skip on, the sensor bench and the microSD card.
Renode: the runs of `examples/ardupilot-bench/renode/bench-renode.sh`; an
earlier set by hand gave 0.299 and 0.306.

### How each was measured

- **Athrill:**
  - two runs of the same workload, one stopped at 20 virtual seconds and one
    at 80;
  - the RTF is 60 s divided by the difference of their wall times;
  - both runs execute the same instructions between the two points, because
    Athrill is deterministic: 2,756,787,092 with the branch rule, 2,756,316,759
    with the idle function named (the branch rule executes the `b .` once
    each time before it halts).
- **Renode:** wall time against `time_boot_ms` in ArduPilot's own MAVLink
  messages.

**The MIPS setting barely changes Renode's speed.** Athrill's speed does not
depend on the configured CPU frequency either: the firmware executes the same
instructions.

### The peripherals differ

| | Athrill STM32 | Renode (ArduPilot's generated Pixhawk 4 platform) |
| --- | --- | --- |
| IMUs | ICM-20689 | ICM-20689, ICM-20602, BMI055 |
| Compass | IST8310 | none |
| Barometer | MS5611 | MS5611 |
| GPS | u-blox modelled, not detected by this firmware | none |
| IOMCU | none (`BRD_IO_ENABLE=0`) | modelled, unused with `BRD_IO_ENABLE=0` |
| microSD | yes | yes |

Renode serves two more IMUs; Athrill serves a compass. The firmware and the
request streams are the same, but the sensor work of the two firmwares is
not proven identical.

## The same instruction addresses

Both emulators can write the address (PC) of every executed instruction,
from reset onwards. Athrill writes them in Renode's text format. Both traced
the same ELF with the same F-RAM seed:
- Athrill: 20 million PCs, with the plain boot configuration of the example
  (a branch to itself as `WFI`; no sensor bench, no microSD card).
- Renode: `cpu Step 20000000` at 216 MIPS. It wrote only the first 411,873
  PCs; we have not found out why.

**Results:**
- The **first 338 instructions** are identical.
- **First difference.** `tools/compare_pc_trace.py` stops there: index 339,
  in `stm32_clock_init`, the poll for the RCC to report a clock ready.
- **Past the differences.** `tools/resync_pc_trace.py` goes on past each
  difference. It searches a little ahead in both traces for the next 64
  identical PCs and continues from there.
  - Of Renode's 411,873 instructions, **411,627 were identical in
    lock-step**.
  - The traces diverged 91 times, always on timing:

| Divergences | Where | Why |
| ---: | --- | --- |
| 89 | entry to a timer interrupt from the idle thread | Athrill executes the `b .` once more before it takes the interrupt; Renode takes it at once. |
| 1 | `stm32_clock_init` | The number of polls until the RCC reports a clock ready |
| 1 | `chSysPolledDelayX` | The number of iterations of ChibiOS's busy-wait delay |

**How many idle iterations precede an interrupt is timing, not meaning.**
- The same comparison with the idle function named gave the opposite: 91
  entries where Renode executed the idle loop once more, and 411,536
  instructions identical.
- In both cases, the instruction boundary at which the interrupt arrives
  decides the count.

**What this shows:**
- **Control flow.** Both emulators took the same branches and called the
  same functions, in the same order, through boot and into the scheduler.
- **Instruction decoding.** A mis-decoded instruction would have sent one of
  them down another path.
- **Configuration.** Renode traced with its microSD card attached and
  Athrill without one. No divergence in the traced range involves the card.
- **What it does not show.** The comparison covers addresses only. Register
  and memory contents were not compared, so it is strong evidence, not a
  proof, that the two executions compute the same values.

## Reproduce

Everything below runs on the [test machine](#test-machine). Fetch the
firmware first:

```sh
mkdir -p ~/ardupilot-bench && cd ~/ardupilot-bench
curl -LO https://firmware.ardupilot.org/Copter/stable/Pixhawk4/arducopter.elf
shasum -a 256 arducopter.elf
```

The stable directory moves with each release. If the hash differs from the
one above, the results may differ too.

The helper scripts need python3 with pymavlink:

```sh
python3 -m pip install pymavlink
```

### Athrill

Build `athrill3` as in the [README](../README.md#1-build-athrill-stm32);
the commands below assume this repository at `~/athrill-target-stm32`. Every
run starts from the F-RAM seed and a new microSD image.

```sh
cd ~/athrill-target-stm32
examples/ardupilot-bench/run-full.sh ~/ardupilot-bench/arducopter.elf 4320000000 > /dev/null    # 20 virtual s
examples/ardupilot-bench/run-full.sh ~/ardupilot-bench/arducopter.elf 17280000000 > /dev/null   # 80 virtual s
examples/ardupilot-bench/inspect-mavlink.py examples/ardupilot-bench/mavlink-full.raw
```

Time each of the two runs (for example with `/usr/bin/time -p`). The RTF is 60
divided by the difference of their wall times. For the trace:

```sh
examples/ardupilot-bench/run-trace.sh ~/ardupilot-bench/arducopter.elf
```

### Renode

1. Set up ArduPilot's Renode as in [its
   instructions](https://ardupilot.org/dev/docs/renode-emulation.html), with
   two choices:
   - **Renode:** ArduPilot's performance branch `pr-arudpilot-am32-perf`
     of [ArduPilot/renode](https://github.com/ArduPilot/renode), with
     `perf_patches/apply.sh` applied and built with `./build.sh --net`.
     Stock Renode 1.17.0 does not boot this platform, because its STM32F4
     random-number generator model is missing.
   - **ArduPilot checkout:** one with `Tools/renode`.
2. Run the benchmark: three runs at each CPU setting, each started from this
   repository's F-RAM seed.

```sh
examples/ardupilot-bench/renode/bench-renode.sh ~/ardupilot ~/renode/renode ~/ardupilot-bench/arducopter.elf
```

`examples/ardupilot-bench/renode/trace.resc` writes Renode's PC trace;
its comments give the command.

### Compare the traces

```sh
tools/compare_pc_trace.py examples/ardupilot-bench/athrill-pc-trace.txt renode-pc-trace.txt --elf ~/ardupilot-bench/arducopter.elf
tools/resync_pc_trace.py examples/ardupilot-bench/athrill-pc-trace.txt renode-pc-trace.txt ~/ardupilot-bench/arducopter.elf
```

## Test machine

The figures above were measured on one Mac only:

| | |
| --- | --- |
| Machine | Apple M2 Pro |
| OS | macOS 27.2 |
| Athrill STM32 | this repository's `athrill3`, Release build |
| Renode | ArduPilot's `pr-arudpilot-am32-perf` (`fbea4f40`), `perf_patches` applied, built for arm64 on .NET 10 (`net10.0`), Release |
| ArduPilot (for Renode's `Tools/renode`) | `bc544bf` |
| Runs | one emulator at a time, nothing else heavy on the host |

## What comes next

This comparison is an overture. Next:
- **Hakoniwa for ArduPilot.** Connect ArduPilot on Athrill to external
  physics, as [Beyond SIH](../README.md#beyond-sih-px4s-sensor-drivers-and-external-physics)
  does for PX4. It is not done yet.
- **Release v0.2.0** has PX4 flying in Hakoniwa (see [hakoniwa.md](hakoniwa.md)),
  with the `athrill3` that runs this comparison. ArduPilot flying in
  Hakoniwa is planned for v0.3.0.
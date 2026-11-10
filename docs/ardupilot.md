# ArduPilot on Athrill STM32, and a comparison with Renode

The unmodified ArduCopter firmware for the Pixhawk 4 boots on Athrill STM32,
the same emulated board that runs PX4. ArduPilot also runs this firmware on
[Renode](https://ardupilot.org/dev/docs/renode-emulation.html), so the two
emulators can be compared on the same ELF.

## Results

**Conditions:**
- the same ELF, F-RAM seed and MAVLink stream requests, on one Apple M2 Pro;
- the same IMU: Renode is started with `--num-imus 1`, so both boards serve the ICM-20689 only
  ([The peripherals differ](#the-peripherals-differ));
- both emulators halt at the idle thread's branch to itself (see [The idle loop](#the-idle-loop));
- the real-time factor (RTF) is measured over virtual 20 to 80 s, after the boot.

| | RTF | Against Renode |
| --- | ---: | ---: |
| Renode, ArduPilot's performance branch, one IMU | 0.596 | |
| Athrill STM32, interpreter | 1.24 | 2.1x |
| Athrill STM32, block JIT | 1.95 | 3.3x |
| **Athrill STM32, block JIT and tick skip** | **2.27** | ****3.8x**** |

- With ArduPilot's default platform, Renode serves three IMUs, does about three times the SPI and DMA work, and runs at
  0.30. An earlier version of this page compared Athrill against that, with a different workload.
- Athrill's three configurations give bit-identical MAVLink output and the same number of instructions.
- **Instructions.** Both emulators executed the firmware along the same path,
  instruction address for instruction address. The two traces differ only
  where the firmware waits on hardware timing
  ([The same instruction addresses](#the-same-instruction-addresses)).

## Where the time goes

Both emulators were profiled with macOS `sample` on their emulation thread, in the steady state.
Renode's JIT-compiled .NET methods were named through the .NET perf map (`DOTNET_PerfMapEnabled=1`).

**Per virtual second of the same run:**

| | Renode, one IMU | Athrill, interpreter | Athrill, JIT and tick skip |
| --- | ---: | ---: | ---: |
| Host time | 1.68 s | 0.81 s | 0.44 s |
| ... in the device models | 0.74 s | 0.18 s | 0.11 s |
| ... in the CPU and everything else | 0.94 s | 0.63 s | 0.33 s |
| Guest instructions executed | 51.0 M | 46.2 M | 46.2 M |
| Device register accesses by the CPU | 0.76 M | 1.01 M | 1.01 M |

**Renode's emulation thread** (share of its host time):

| | Share |
| --- | ---: |
| Device models in .NET, reached from the CPU's bus accesses | 44% |
| ... generic register-file models | 21% |
| ... DMA stream transfers | 16% |
| ... SPI | 15% |
| ... interrupt controller | 9% |
| `setjmp` around each call from .NET into the CPU library (on macOS, two system calls each) | 15% |
| The guest code itself (translated blocks) | 14% |
| The time framework (`Antmicro.Renode.Time`) | 5% |
| Garbage collection | 3% |

**Our reading of the difference:**
- **The cost per device access, not the number of accesses.** Athrill makes 1.3 times as many register accesses, and
  spends a quarter or less of Renode's time in its devices.
- In Renode, the device models are .NET and the CPU is a C library (tlib). Each access crosses that boundary, and
  each call back into the CPU library is guarded by a `setjmp`. In Athrill, the CPU and the models are C on one
  thread, and an access is one function call.
- **Not the time framework:** it is 5% of Renode's thread on this workload.
- **Not the idle loop:** both emulators halt at it.
- Renode's choice buys breadth: many architectures, multi-node systems, and device models that are easy to write in
  C#. Athrill's choices are described in [Speed and fidelity](speed-and-fidelity.md).
- The `setjmp` share was measured on macOS only; with Linux's C library it may be smaller.

**The device models also differ.** Athrill's SPI model is accessed about seven times as often as Renode's: its
transfer completes at once, and the firmware polls its status register more. What each Athrill model reproduces is in
[Speed and fidelity](speed-and-fidelity.md#four-models-in-detail).

## The idle loop

ArduCopter is idle for most of its clocks. Its idle thread is a branch to itself (`b .`, the halfword `e7fe`), and the
firmware contains no `WFI` or `WFE`.
- **Athrill** halts the core at a branch to itself (`DEVICE_CONFIG_SPIN_HALT`) and jumps the clock to the next device
  event. The rule uses only the meaning of the instruction: a `b .` can only be left by an interrupt.
- **Renode does the same:** its CPU library translates a Thumb `b .` as `WFI` (tlib `arch/arm/translate.c`).
  Measured, it executes 51.0 M instructions per virtual second at 216 MIPS, not 216 M.
- So the comparison above is idle against idle. With its idle loop executed instead, Athrill runs at 1.19 (interpreter)
  and 2.15 (JIT and tick skip) over 0 to 80 virtual s, with the same MAVLink output.

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
| Renode, ArduPilot's performance branch, `--num-imus 1`, 462 MIPS | 0.598 | 0.591 | 0.598 | **0.596** |
| Renode, the same with its default three IMUs, 462 MIPS | 0.304 | 0.296 | | 0.30 |
| Athrill STM32, interpreter | 1.239 | 1.240 | | **1.24** |
| Athrill STM32, block JIT | 1.946 | 1.944 | | **1.95** |
| Athrill STM32, block JIT and tick skip | 2.271 | 2.265 | | **2.27** |

Measured 2026-11-10 and 11. Athrill: 216 MHz, a branch to itself as `WFI`, the clock skip on, the sensor bench and the
microSD card. The second Renode run with three IMUs was taken with the sampler attached.

On 2026-11-05, before the boards were built from Devicetree, the Athrill interpreter measured 1.583 on this workload,
and Renode with three IMUs 0.300 at 462 MIPS and 0.307 at 216 MIPS.

### How each was measured

- **Athrill:**
  - two runs of the same workload, one stopped at 20 virtual seconds and one
    at 80;
  - the RTF is 60 s divided by the difference of their wall times;
  - both runs execute the same instructions between the two points, because
    Athrill is deterministic: 2,772,625,818.
- **Renode:** wall time against `time_boot_ms` in ArduPilot's own MAVLink
  messages.

**The MIPS setting does not change how many instructions Renode executes here.** Renode halts at the idle loop, so
the firmware runs the same work whatever the setting: 2.14 M device accesses per virtual second at 462 MIPS and
2.09 M at 216 MIPS (three IMUs).

### The peripherals differ

| | Athrill STM32 | Renode (ArduPilot's generated Pixhawk 4 platform) |
| --- | --- | --- |
| IMUs | ICM-20689 | ICM-20689, ICM-20602, BMI055 |
| Compass | IST8310 | none |
| Barometer | MS5611 | MS5611 |
| GPS | u-blox modelled, not detected by this firmware | none |
| IOMCU | none (`BRD_IO_ENABLE=0`) | modelled, unused with `BRD_IO_ENABLE=0` |
| microSD | yes | yes |

With its defaults, Renode serves two more IMUs, and ArduPilot reads every IMU it finds: about three times the SPI
and DMA work of Athrill. Started with `--num-imus 1`, Renode serves the ICM-20689 only, and the two firmwares then
access DMA2, RCC and the chip-select GPIO port almost the same number of times: DMA2 exactly, RCC within 3% (counted below). Athrill still
serves a compass, which Renode does not.

### Counting the device accesses

Per virtual second, virtual 20.5 to 21.5 s:

| Peripheral | Renode, one IMU (read / write) | Athrill (read / write) |
| --- | ---: | ---: |
| TIM2 | 176,715 / 20,730 | 162,641 / 19,396 |
| DMA2 | 61,300 / 124,900 | 61,300 / 124,900 |
| SPI1 | 12,300 / 28,700 | 132,500 / 148,900 |
| RCC | 64,510 / 43,000 | 65,995 / 43,990 |
| GPIOG (IMU chip select) | 41,297 / 45,100 | 41,300 / 45,100 |
| GPIOF | 99 / 8,600 | 100 / 8,600 |
| I2C3 (compass) | 0 / 0 | 5,148 / 4,158 |
| NVIC and system control space | 118,110 | 125,994 |
| **Total** | **761,215** | **1,008,482** |

- **Renode:** its machine profiler (`machine EnableProfiler`), turned on at virtual 20 s from the monitor; the entries
  are counted by virtual time.
  The table is a run at 216 MIPS. Renode's counts vary from run to run: `count-renode.sh` at 462 MIPS counted 791,361
  accesses and 51.7 M instructions in another run.
- **Athrill:** a counting build: `examples/ardupilot-bench/renode/count-athrill.patch` adds counters to the target's
  device access functions. Two runs, to 20.5 and to 21.5 virtual s; the difference is the window.
  The table was counted with the JIT and tick skip on; the interpreter counts 0.05% fewer reads, with the same output.


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

Build `athrill3` as in the [README](../README.md#1-build-athrill-stm32), and `build-fast/athrill3` with the JIT and
tick skip as at the end of [Build from source](../README.md#build-from-source). The commands below assume this
repository at `~/athrill-target-stm32`. Every run starts from the F-RAM seed and a new microSD image.

The RTF of the interpreter, from two runs to 20 and to 80 virtual s:

```sh
cd ~/athrill-target-stm32
/usr/bin/time -p examples/ardupilot-bench/run-full.sh ~/ardupilot-bench/arducopter.elf 4320000000 > /dev/null    # 20 virtual s
/usr/bin/time -p examples/ardupilot-bench/run-full.sh ~/ardupilot-bench/arducopter.elf 17280000000 > /dev/null   # 80 virtual s
examples/ardupilot-bench/inspect-mavlink.py examples/ardupilot-bench/mavlink-full.raw
```

The RTF is 60 divided by the difference of the two `real` times. The same with the JIT and tick skip:

```sh
printf 'DEVICE_CONFIG_STM32_JIT 1\nDEVICE_CONFIG_DEVICE_TICK_SKIP 1\n' > /tmp/athrill-fast.txt
/usr/bin/time -p examples/ardupilot-bench/run-full.sh ~/ardupilot-bench/arducopter.elf 4320000000 build-fast/athrill3 /tmp/athrill-fast.txt > /dev/null
/usr/bin/time -p examples/ardupilot-bench/run-full.sh ~/ardupilot-bench/arducopter.elf 17280000000 build-fast/athrill3 /tmp/athrill-fast.txt > /dev/null
```

For the JIT alone, leave out the second line of `/tmp/athrill-fast.txt`.

**Where the time goes.** Start the 80-second run with the JIT and tick skip, and sample it for 30 s from 15 s after
the start:

```sh
examples/ardupilot-bench/run-full.sh ~/ardupilot-bench/arducopter.elf 17280000000 build-fast/athrill3 /tmp/athrill-fast.txt > /dev/null &
sleep 15
sample "$(pgrep -x athrill3)" 30 -file /tmp/athrill.sample.txt > /dev/null
wait
examples/ardupilot-bench/renode/sample-share.py /tmp/athrill.sample.txt main-thread \
  --group 'device ticks=^athrill_device_tick ' --group 'register accesses=^mmio_(get|put)' \
  --group 'JIT code=^translated or JIT code$'
```

**Counting the device accesses.** `examples/ardupilot-bench/renode/count-athrill.patch` adds counters to the device
access functions. It is for this measurement only; build it in a second checkout next to the first, so that it finds the same sibling repositories:

```sh
git -C ~/athrill-target-stm32 worktree add ~/athrill-target-stm32-count
git -C ~/athrill-target-stm32-count apply examples/ardupilot-bench/renode/count-athrill.patch
cmake -S ~/athrill-target-stm32-count -B ~/athrill-target-stm32-count/build -DCMAKE_BUILD_TYPE=Release
cmake --build ~/athrill-target-stm32-count/build --parallel --target athrill3
CNT_PAGES=/tmp/pages-20.5.txt examples/ardupilot-bench/run-full.sh ~/ardupilot-bench/arducopter.elf 4428000000 ~/athrill-target-stm32-count/build/athrill3 2>&1 > /dev/null | grep '^COUNT'
CNT_PAGES=/tmp/pages-21.5.txt examples/ardupilot-bench/run-full.sh ~/ardupilot-bench/arducopter.elf 4644000000 ~/athrill-target-stm32-count/build/athrill3 2>&1 > /dev/null | grep '^COUNT'
```

Each `COUNT` line gives the totals from reset; the difference of the two is the window from 20.5 to 21.5 virtual s.
The page files give the reads and writes per 256-byte page.

### Renode

1. Set up ArduPilot's Renode as in [its
   instructions](https://ardupilot.org/dev/docs/renode-emulation.html), with
   two choices:
   - **Renode:** ArduPilot's performance branch `pr-arudpilot-am32-perf`
     of [ArduPilot/renode](https://github.com/ArduPilot/renode), with
     `perf_patches/apply.sh` applied and built with `./build.sh --net`
     (on Apple Silicon, `./build.sh --net --host-arch aarch64`).
     Stock Renode 1.17.0 does not boot this platform, because its STM32F4
     random-number generator model is missing.
   - **ArduPilot checkout:** one with `Tools/renode`.
2. Run the benchmark: three runs with one IMU and three with the default three IMUs, each started from this
   repository's F-RAM seed. Run one Renode at a time: each script stops every Renode process when it ends.

```sh
examples/ardupilot-bench/renode/bench-renode.sh ~/ardupilot ~/renode/renode ~/ardupilot-bench/arducopter.elf
```

3. Where the time goes, with one IMU (macOS `sample`, 30 s in the steady state):

```sh
examples/ardupilot-bench/renode/profile-renode.sh ~/ardupilot ~/renode/renode ~/ardupilot-bench/arducopter.elf
```

4. The device accesses, interrupts and instructions per virtual second, with one IMU (Renode's machine profiler):

```sh
examples/ardupilot-bench/renode/count-renode.sh ~/ardupilot ~/renode/renode ~/ardupilot-bench/arducopter.elf
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
| Athrill STM32 | this repository's `athrill3` and `build-fast/athrill3`, Release builds, with athrill `a63d4cd`, athrill-arm-common `60d8098`, athrill-device `ab4de28` |
| Renode | ArduPilot's `pr-arudpilot-am32-perf` (`fbea4f40`), `perf_patches` applied, built for arm64 on .NET 10 (`net10.0`), Release |
| ArduPilot (for Renode's `Tools/renode`) | `921002b` (2026-11-10 and 11); `bc544bf` for the trace |
| Runs | one emulator at a time, nothing else heavy on the host |

## What comes next

This comparison is an overture. Next:
- **Hakoniwa for ArduPilot: done on main** (for v0.3.0). The same ELF flies
  the Hakoniwa Drone x500 ([hakoniwa.md](hakoniwa.md#ardupilot),
  `examples/ardupilot-hakoniwa`). Release v0.2.0 has PX4 flying in Hakoniwa
  and the `athrill3` of this comparison.

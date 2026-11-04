# ArduPilot on Athrill STM32, and a comparison with Renode

The unmodified ArduCopter firmware for the Pixhawk 4 boots on Athrill STM32,
the same emulated board that runs PX4. ArduPilot also runs this firmware on
[Renode](https://ardupilot.org/dev/docs/renode-emulation.html), so the
two emulators can be compared on the same ELF.

**Result:**
- Athrill ran the firmware **about 5 times as fast** as ArduPilot's
  performance-patched Renode on the same Mac: real-time factor 1.56 against
  0.30.
- Both emulators executed **the same instruction stream**. Of the first
  411,872 instructions that Renode traced, 411,536 were identical, in the same
  order. The traces differed only where the firmware waits on hardware timing.

## The firmware

| | |
| --- | --- |
| Firmware | ArduCopter V4.7.2, `Pixhawk4`, from [firmware.ardupilot.org](https://firmware.ardupilot.org/Copter/stable/Pixhawk4/) (`arducopter.elf`, git `fb773567`) |
| SHA-256 | `cfa1b3f04f326ab91760c53b93503b669d4d62e2d43dd1ffc4d09cb15198eb2c` |
| Changes | none |

What Athrill needed for ArduPilot, beyond PX4:

- **The DWT cycle counter.** ChibiOS reads `DWT_CYCCNT` at start-up. It now
  counts Athrill's virtual clock.
- **UART transmit DMA.** ArduPilot sends MAVLink with DMA. This is off by
  default, so the PX4 runs and their published hashes stay as they were.
- **A board without the IO coprocessor.** The Pixhawk 4's IOMCU is not
  emulated. Without it, ArduPilot fails to update the IO firmware and stays in
  its configuration-error loop. The example's F-RAM seed therefore stores
  `BRD_IO_ENABLE=0`, the normal ArduPilot setting for a board used without an
  IOMCU. The seed is parameter data that ArduPilot itself wrote. It contains
  no ArduPilot code.

What ArduPilot finds on the board:
- the ICM-20689 IMU;
- the IST8310 compass;
- the MS5611 barometer;
- the microSD card, where it writes its storage backup.

It does not find the second IMU (the BMI055 is not emulated) or the u-blox
GPS. No ArduPilot, ChibiOS or Renode code is in this repository.
[examples/ardupilot-bench](../examples/ardupilot-bench/README.md) has the run
scripts.

## Speed

**Conditions:**
- Host: one Mac (Apple M2 Pro, macOS 27.2).
- Firmware: the same ELF in both emulators.
- F-RAM: the same seed with `BRD_IO_ENABLE=0` in both, restored before every
  run.
- Ground station: both emulators get heartbeats and requests for ArduPilot's
  normal MAVLink streams, so both firmwares do the same telemetry work.
- Measured interval: virtual time 20 s to 80 s, after the boot.
- Real-time factor (RTF): virtual seconds divided by wall seconds.

| | Run 1 | Run 2 | Run 3 | Mean |
| --- | ---: | ---: | ---: | ---: |
| **Athrill STM32** (216 MHz, idle skip, sensor bench and microSD) | 1.571 | 1.548 | 1.561 | **1.560** |
| Renode, ArduPilot's performance branch, 462 MIPS (ArduPilot's default) | 0.300 | 0.297 | 0.300 | 0.299 |
| Renode, the same, 216 MIPS (Athrill's setting) | 0.309 | 0.297 | 0.311 | 0.306 |

**Renode:** ArduPilot's fork, branch `pr-arudpilot-am32-perf` (`fbea4f40`),
with its `perf_patches` applied. It was built for arm64 and started by
ArduPilot's `Tools/renode/run.py Pixhawk4 --unthrottled`. Stock Renode
1.17.0 does not boot this platform: its STM32F4 random-number generator model
is missing.

**How each was measured:**
- **Athrill:** two runs of the same workload, one stopped at 20 virtual
  seconds and one at 80. The RTF is 60 s divided by the difference in their
  wall times. Athrill is deterministic, so both runs execute the same
  instructions: 2,756,316,759 between the two points.
- **Renode:** wall time against `time_boot_ms` in ArduPilot's own MAVLink
  messages.

**Results:**
- **The MIPS setting barely changes Renode's speed.** Athrill's speed does not
  depend on the configured CPU frequency either, because the firmware executes
  the same instructions in either case.
- **The difference is in how time advances.**
  - When the firmware waits, Athrill skips the virtual clock to the next
    device event. Its devices compute their state from the virtual clock when
    the firmware reads them.
  - Renode executes the idle loop. It advances devices and timers in quanta of
    virtual time.

**Not the same in both emulators:**
- **Peripherals.** Renode's platform models more of the board. ArduPilot on
  Athrill misses the BMI055 and the GPS, which is less driver work. Athrill
  ran its sensor bench (live IMU, compass and barometer data) and the microSD
  card in all three runs.
- **The workload is not proven identical.** The firmware and the request
  streams are the same. How much each firmware does with its sensors is not
  identical.

## The same instructions

Both emulators can write the PC of every executed instruction, from reset
onwards. Athrill writes them in Renode's format. Each emulator wrote its trace
of the same ELF and F-RAM seed:
- Athrill: `examples/ardupilot-bench/run-trace.sh`, 20 million PCs.
- Renode: `cpu CreateExecutionTracing ... PC` and `cpu Step 20000000`, 216
  MIPS. Renode wrote only the first 411,872 PCs; we have not found out why.

**Results:**
- The **first 338 instructions** are identical.
- `tools/compare_pc_trace.py` stops at the first difference: index 339, in
  `stm32_clock_init`, the RCC clock-ready poll.
- `tools/resync_pc_trace.py` goes on past each difference. It searches a
  little ahead in both traces for the next 64 identical PCs and continues from
  there. Of Renode's 411,872 instructions, **411,536 were identical in
  lock-step**.
- The traces diverged 93 times, always on timing, never on the decoding or
  the result of an instruction:

| Divergences | Where | Why |
| ---: | --- | --- |
| 91 | entry to a timer interrupt (`VectorB0`) | Renode executes one more instruction of the idle thread before taking the interrupt. Athrill skips the idle time and takes the interrupt at once. |
| 1 | `stm32_clock_init` | The number of polls until the RCC reports a clock ready |
| 1 | `chSysPolledDelayX` | The number of iterations of ChibiOS's busy-wait delay |

So the two emulators run the same firmware through the same instruction
stream. They differ in how virtual time advances, and Athrill is about 5 times
as fast.

## Reproduce

Athrill:

```sh
examples/ardupilot-bench/run-full.sh arducopter.elf 17280000000   # 80 virtual seconds
examples/ardupilot-bench/run-trace.sh arducopter.elf               # PC trace
```

Renode: follow [ArduPilot's Renode
instructions](https://ardupilot.org/dev/docs/renode-emulation.html) with the
performance branch above. Before each run, copy
`examples/ardupilot-bench/fram-brd-io-disabled.bin` to the F-RAM file in the
state directory. Then run:

```sh
Tools/renode/run.py Pixhawk4 --elf arducopter.elf --unthrottled \
  --exec "cpu PerformanceInMips 216"
```

Compare the traces:

```sh
tools/compare_pc_trace.py athrill-pc-trace.txt renode-pc-trace.txt --elf arducopter.elf
tools/resync_pc_trace.py athrill-pc-trace.txt renode-pc-trace.txt arducopter.elf
```

# Performance notes

Where the time goes when PX4 runs on the STM32 target, what has been done
about it, and what is left. Numbers are from a MacBook (Apple silicon),
PX4 `px4_fmu-v5_default`, 216 MHz virtual clock.

## 1. Measurements

| Measure | Before (2026-11-04 morning) | Now |
|---|---|---|
| CPU-bound loop (200 M instructions, `ldr/str/add/push/bl/...`) | 3.8 s | 2.07 s (about 97 M instructions/s) |
| PX4, first 1 G clocks (boot, CPU busy) | 150 s at the start of the day; 7.0 s after idle skip and batching | 4.0 s |
| PX4, steady state RTF (no sensors, PX4 mostly idle) | 0.84 → 3 to 4.7 | about 6 to 7 |

A real Cortex-M7 at 216 MHz runs roughly 200 M instructions/s, so a fully
loaded CPU would run at RTF about 0.5. PX4 with sensors and the estimator is
far from fully loaded, so the RTF to expect lies between; measure it once the
sensors are attached.

## 2. What was done, in order

1. **Idle skip**: the core halts at the OS idle function
   (`DEVICE_CONFIG_IDLE_FUNCTION`, `up_idle` by default) until an exception
   is pending, so Athrill skips clocks to the next device event.
2. **Batched execution** (`cpu_supply_clocks`, 1024 instructions): devices
   tick between batches only; a batch ends at the next device event the
   devices reported, at a halt, or at a register access that may change the
   next event (writes, and reads with side effects such as a UART's RDR).
   Devices catch up with the clock when accessed.
3. **Timer and UART models** that compute time arithmetically (whole periods
   skipped) instead of counting per clock.
4. **Decode cache**: the executor of the instruction at a PC, validated
   against the instruction's halfwords in host memory.
5. **Memory windows**: fetches, loads and stores in ROM/RAM go to host memory
   directly, without the bus and the MPU region search.
6. **Lean hot path**: `thumb_execute` handles only the common step;
   exceptions, errors and what an instruction asks for afterwards are out of
   line; the batch loop calls it without `cpu_supply_clock`.
7. **Exception check only after a change** (`pending_check` in
   athrill-arm-common): an exception left pending but not taken is not
   scanned for before every instruction.
8. Small ones: PC sampling without a division per instruction; rare paths
   (`take_reset`, error reports) kept out of line so their stack frames are
   not set up per instruction.

The console output of PX4 is identical before and after each step.

## 3. What is left

- **The pseudo-code executors** take most of the remaining time: each
  instruction fills an input structure (register numbers, names, values) and
  an output structure, and the pseudo-code functions follow the Arm
  pseudo-code closely. This is deliberate (readability, checkability against
  the manual). A next round could add specialised fast executors for the
  most frequent instructions (loads and stores with immediate offsets,
  `push`/`pop`, `add`/`sub`/`cmp` immediate, branches), checked against the
  pseudo-code versions. To be decided with the measured RTF under load.
- **Straight-line blocks** (decision 7 of the design): run a cached sequence
  of instructions without returning to the batch loop between them.
- **Device ticks** during boot: the timer and UART ticks loop over all
  instances; a list of active ones would cut that.
- **Check against one-instruction-at-a-time execution**: a build with
  `ATHRILL_TARGET_BATCH_CLOCKS=1` should give the same console output.
  Ending batches only at writes changes when devices tick, so the total
  instruction count at a fixed clock can differ by a few instructions (idle
  wake-up points), while the output stays the same; this needs a written
  rule of what "identical" means.

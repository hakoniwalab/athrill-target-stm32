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

## 4. Round 2: PX4 SIH under load (2026-11-04 afternoon)

PX4 SIH hovering (`docs/sih.md`): the CPU executes about 23 % of the
virtual clocks (2.72 G instructions in 12 G clocks, 55.6 s), the rest is
skipped idle time. RTF is then the host's instructions per second divided by
about 49 M (216 MHz x 0.23): 57 M/s gave RTF 1.17.

| Step | Host instructions/s | RTF (SIH hover, without / with SD logging over MAVLink) |
|---|---|---|
| Start | 57 M | 1.17 / 1.19 |
| Priority-only check after MSR/CPS (athrill-arm-common) | 65 M | 1.33 |
| Code window apart from the data windows; PC sampling only on request | 67 M | 1.37 |
| Decode cache 2^16 to 2^20 entries | 72 M | 1.48 / 1.47 |

1. **MSR and CPS** (NuttX's critical sections write BASEPRI twice each) set a
   priority-only check: the pending exception of the highest priority found
   by the last full scan is compared with the new execution priority, and
   with none pending nothing is scanned. Before, each MSR made the next
   instruction scan all exceptions (`execute_slow` 11 % of the host time).
   `ArmMExecutionPriority` also stops at the implemented interrupts instead
   of 512.
2. **Code window**: instruction fetch has its own memory window; with one
   pair of windows shared with loads and stores, a fetch after a RAM access
   missed and swapped windows.
3. **PC sampling** (`record_sample`, a linear search) runs only with
   `DEBUG_PC_SAMPLE_START`; the instruction count is kept.
4. **Decode cache** of 2^20 entries (32 MB of host memory): one entry per
   halfword of the 2 MB of flash, no conflicts in PX4's code.

The host profile now: `thumb_execute` itself 22 %, the batch loop 11 %, the
floating-point pseudo-code (`FPRoundMode`, `ArmFPArith`, `FPMulAdd`, ...)
about 13 %, the integer executors the rest. The next steps (a host-FPU fast
path for the floating-point instructions, specialised executors for the
most frequent instructions) trade the closeness to the Arm pseudo-code for
speed and are left to decide.

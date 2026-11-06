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
   (`DEVICE_CONFIG_IDLE_FUNCTION`, set to `up_idle` in the PX4 examples) until an exception
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

## 3. What was left after round 1

Kept as written after round 1, with what became of each (round 2, section 4).

- **The pseudo-code executors** take most of the remaining time: each
  instruction fills an input structure (register numbers, names, values) and
  an output structure, and the pseudo-code functions follow the Arm
  pseudo-code closely. This is deliberate (readability, checkability against
  the manual). A next round could add specialised fast executors for the
  most frequent instructions, checked against the pseudo-code versions.
  *Done in round 2: fast executors of 30 encodings and the host-FPU path,
  both with a strict mode.*
- **Straight-line blocks** (decision 7 of the design): run a cached sequence
  of instructions without returning to the batch loop between them.
  *Still open: the next step for speed.*
- **Device ticks** during boot: the timer and UART ticks loop over all
  instances; a list of active ones would cut that. *Still open.*
- **Check against one-instruction-at-a-time execution**: a build with
  `ATHRILL_TARGET_BATCH_CLOCKS=1` should give the same console output.
  Ending batches only at writes changes when devices tick, so the total
  instruction count at a fixed clock can differ by a few instructions (idle
  wake-up points), while the output stays the same; this needs a written
  rule of what "identical" means. *Still open. Identity between runs of the
  same build, and between the fast and strict modes, is shown
  ([verification](verification.md#determinism)).*

## 4. Round 2: PX4 SIH under load (2026-11-04 afternoon)

PX4 SIH hovering (`docs/sih.md`): the CPU executes about 23 % of the
virtual clocks (2.72 G instructions in 12 G clocks, 55.6 s), the rest is
skipped idle time. RTF is then the host's instructions per second divided by
about 49 M (216 MHz x 0.23): 57 M/s gave RTF 1.17.

| Step | Host instructions/s | RTF (SIH hover) |
|---|---|---|
| Start | 57 M | 1.17 |
| Priority-only check after MSR/CPS (athrill-arm-common) | 65 M | 1.33 |
| Code window apart from the data windows; PC sampling only on request | 67 M | 1.37 |
| Decode cache 2^16 to 2^20 entries | 72 M | 1.48 |
| Floating point on the host FPU where exact (fast mode) | 78 M | 1.61 |
| Fast executors of the frequent encodings (fast mode) | 88 M | 1.80 |
| Instructions in a row in `thumb_execute_run`, counters added after | 91 M | 1.86 |
| Fast PUSH/POP, condition table | 92 M | 1.88 |

With MAVLink over TCP and SD logging (the way it is used): RTF 1.19 at the
start, 1.94 at the end.

1. **MSR and CPS** (NuttX's critical sections write BASEPRI twice each) set a
   priority-only check: the pending exception of the highest priority found
   by the last full scan is compared with the new execution priority, and
   with none pending nothing is scanned. Before, each MSR made the next
   instruction scan all exceptions (`execute_slow` 11 % of the host time).
   `ArmMExecutionPriority` also stops at the implemented interrupts instead
   of 512.
2. **Code window**: instruction fetch has its own memory window; with one
   pair of windows shared with loads and stores, a fetch after a RAM access
   missed and swapped windows. Writes go through a window only to RAM: a
   store to flash goes to the bus, which refuses it as before the windows
   (they had let stores modify flash). Decoded instructions from flash are
   then not checked against memory again.
3. **PC sampling** (`record_sample`, a linear search) runs only with
   `DEBUG_PC_SAMPLE_START`; the instruction count is kept.
4. **Decode cache** of 2^20 entries (32 MB of host memory): one entry per
   halfword of the 2 MB of flash, no conflicts in PX4's code.
5. **Floating point** (`DEVICE_CONFIG_ARM_FP_MODE`, athrill-arm-common
   `arm_fp_host.h`): the arithmetic runs on the host's FPU where IEEE 754
   guarantees the pseudo-code's bits and FPSCR (round to nearest, normal or
   zero operands and results); IXC is worked out exactly (TwoSum, FMA error
   terms) because writing the host's flag register is slow on Apple
   silicon. 2.8 M random cases against the pseudo-code.
6. **Fast executors** (`DEVICE_CONFIG_ARM_EXEC_MODE`, `src/cpu/thumb_fast.c`)
   of the 30 most frequent encodings (B, CMP, MOV, LDR/STR forms, BL, PUSH,
   POP, ...: three quarters of the instructions PX4 SIH executes), with the
   same profile functions as the pseudo-code but no input/output
   structures. `test/cpu/test_thumb_fast.c` compares them with the generic
   executors on 5.3 M random encodings and states (core state and memory).
7. **The run loop** (`thumb_execute_run`): instructions in a row with the
   step inlined; the cycle and elapsed counters are added after the run.

Strict mode (`DEVICE_CONFIG_ARM_FP_MODE strict`, `DEVICE_CONFIG_ARM_EXEC_MODE
strict`) keeps the pseudo-code-only path: every QEMU instruction test runs in
both modes, and PX4 SIH takes off in both (strict about 25 % slower).

Where the time goes now: the step itself (`execute_one`: the decode cache,
the indirect call) about a third, the device ticks between batches about a
tenth, the floating-point loads and stores and the remaining generic
executors the rest. About a quarter of PX4's instructions are its byte-wise
`memset` loop, as on the board. The next step would be straight-line blocks
(decision 7 of the design).

# Virtual time and the clock skip

This note describes how Athrill advances time. The design dates from
Athrill's first versions; its parameters were published in 2018 in the
[Athrill function manual](https://qiita.com/kanetugu2018/items/ff50704ed7436e0a20db)
(in Japanese, `DEBUG_FUNC_ENABLE_SKIP_CLOCK`). Within the project the clock
skip is nicknamed "King Crimson", after the ability in *JoJo's Bizarre
Adventure* that erases time while everything lands where it would have.

It also records what the design buys: speed, exact results, and the same
results on every host. [docs/ardupilot.md](ardupilot.md) measures it against
Renode.

## The model

The whole emulated board has **one clock: an integer count of CPU clocks**.
- Each executed instruction advances it by 1.
- Nothing in the emulator reads the host's wall clock. Optional real-time
  pacing is the one exception, and it only sleeps; it never changes a result.
- At 216 MHz, 216,000,000 clocks are one virtual second.

**Devices own their timing.** A device does not run on a thread or a timer
of its own. It keeps the clock at which its state was last brought up to
date, and two calls work on it:

- **Access.** When the firmware reads or writes one of its registers, the
  device first computes its state up to the current clock
  (`cpuemu_get_clock()`). Then it answers. Examples:
  - a timer's `CNT` is the clocks since it started, divided by its
    prescaler;
  - `DWT_CYCCNT` is the clocks since it was enabled;
  - a UART knows from the frame time whether a byte has left the shift
    register.
- **Tick.** At every step of the main loop, the device brings itself up to
  the clock and reports how many clocks remain until its **next event**:
  an interrupt it will raise, a byte that will arrive, a sensor sample. The
  emulator keeps the minimum over all devices, `min_intr_interval`.

Hakoniwa, when connected, takes part as one more device. It caps the
interval at the virtual time up to which the outside world (the physics) has
granted the emulator to run.

## The main loop

```
loop:
    tick all devices                    -> min_intr_interval
    if the CPU runs:
        execute instructions, one clock each, up to min_intr_interval;
        stop early at a device access or when the CPU halts
    if the CPU is halted and the clock skip is on:
        clock += min_intr_interval      # the clock skip
    else:
        clock += 1
```

**Batching.** While the CPU runs, it executes up to 1,024 instructions
between device ticks, never past `min_intr_interval`. It stops early when
it touches a device register, because an access can move a device's next
event: the device then ticks again and reports anew. Between the touches,
no device can change anything, so the result is the same as ticking every
device at every clock.

**The clock skip.** The CPU halts at `WFI` or `WFE`. Until the next device
event, nothing can happen:
- the CPU is halted;
- no device has an event before then;
- no input arrives before then, because input is a device event too.

So the clock jumps straight to that event. No instruction is executed and no
device is ticked in between.

**The skip is exact.** The firmware observes the same register values and the
same interrupts at the same clocks as if every idle clock had been executed.
Because time is a pure function of the instruction stream and the device
events, a run is **deterministic**: the same ELF, configuration and input
files give the same instructions, clocks and output on every run and on
every host. [docs/verification.md](verification.md) checks this bit for bit
on a whole PX4 flight.

## Idle loops without WFI

Many RTOS idle loops never execute `WFI`:
- **NuttX on PX4:** `up_idle` is empty.
- **ChibiOS in ArduPilot's Pixhawk 4 build:** `__idle_thread` is a single
  branch to itself, `b .`. The whole ArduCopter ELF contains no `WFI` or
  `WFE` at all, so without help no emulator can skip its idle time: the CPU
  spins in `b .` until the next interrupt, as on the real board.

Executed faithfully, such a loop burns host time on instructions that change
nothing. Athrill STM32 offers two ways to treat it as `WFI`:

- **A branch to itself** (`DEVICE_CONFIG_ARM_SPIN_HALT 1`, off by default). After executing
  a `B` or `B<cond>` whose target is its own address, outside an IT block,
  the core halts. Only an exception can leave such a branch, so this is a
  rule about the instruction set: it needs no symbols and works on a
  stripped ELF. It covers ChibiOS's `b .`, but not NuttX's loop around an
  empty function.
- **The idle function's name** (`DEVICE_CONFIG_IDLE_FUNCTION`: `up_idle`, NuttX's,
  by default; `none` for none). This covers
  any idle loop, including NuttX's, but needs the firmware's symbol table
  and the name.

With the idle function named, when the PC reaches that address with no
exception to take, the core halts as at `WFI`. The next device event wakes
it as an interrupt wakes a real core. The OS reaches its idle function only when no thread is ready, so this
halt changes no result. It only removes the spin. The ArduPilot measurement
below confirms this: the output with and without it is identical.

## What it is worth: ArduCopter on the Pixhawk 4

**Setup:**
- Firmware: the unmodified ArduCopter V4.7.2 ELF.
- Board: the emulated Pixhawk 4 at 216 MHz.
- Workload: the sensor bench, the microSD card and ground-station stream
  requests.
- Interval: virtual time 20 s to 80 s, which is 12.96 billion clocks.
- Host: Apple M2 Pro.

| Athrill configuration | Instructions executed | Real-time factor | MAVLink output |
| --- | ---: | ---: | --- |
| A branch to itself halts as `WFI` (`DEVICE_CONFIG_ARM_SPIN_HALT 1`), clock skip | 2,756,787,092 (21% of the clocks) | **1.583** | reference |
| The idle function named (`__idle_thread`), clock skip | 2,756,316,759 | 1.592 | identical |
| No idle function (`none`): the core never halts, so the clock skip never happens; `b .` is executed | 12.96 billion (every clock) | 0.62 | identical |
| Idle function, clock skip off (`DEBUG_FUNC_ENABLE_SKIP_CLOCK 0`), before the idle function was ignored without the skip: the halted core waits one clock per loop | as released | about 0.02 (only 0 to 20 virtual s measured, boot included: 986 s wall) | identical up to 20 s |

**What the table shows:**
- **ArduCopter is idle for 79% of its clocks.** Executing them instead of
  skipping them makes the run 2.5 times slower. The output does not change.
- **The skip is a jump, not a fast loop.** With the idle function but no
  skip, the halted core walks through the idle time one clock at a time.
  - Every clock of that walk ticks all the devices. That is far more work
    than an instruction, which runs in batches of up to 1,024 between device
    ticks.
  - So halting without skipping is the slowest of the three, 30 times slower
    than executing the idle loop. A halt pays off only together with the
    skip.
  - This measures the main loop, not the skip: the batches run only while
    the core runs. The combination is pointless, so Athrill STM32 now
    ignores the idle function without the skip (see [Limits](#limits)).
- **Even with every idle clock executed, Athrill's 0.62 is twice Renode's
  0.30** on the same firmware. The second reason is the cost of each device
  access, discussed next.

## Compared with a quantum-based emulator

Renode advances time differently:
- **CPU:** runs for a quantum of instructions.
- **Time framework:** then synchronises the devices and timers. Timers are
  entries in clock sources that this framework advances.
- **Device access:** a register access is dispatched through the system bus
  framework.

Renode does skip time at `WFI`. A busy idle loop such as ChibiOS's `b .` is
executed.

ArduPilot's own performance work on Renode reports where its time goes:
- **The CPU is cheap:** guest CPU emulation is 1–3% of the cost.
- **Each event and each register access is expensive:** a fixed cost of
  about 2–3 µs per register access, and clock-source bookkeeping for each
  timer event.
- Sources: [Renode performance lessons, ArduPilot's performance
  branch](https://github.com/ArduPilot/renode/blob/pr-arudpilot-am32-perf/perf_lessons.md),
  and ArduPilot's `Tools/renode/README.md`.

That README also records a dead end:
- A timer whose `CNT` is computed from elapsed virtual time made the
  emulation about 3 times *slower*.
- The cause: Renode's virtual time advances only at quantum granularity for
  the running CPU. Every microsecond wait in the firmware therefore became a
  busy spin of a whole quantum.
- The README names a cheap virtual-time primitive *within* a quantum as the
  biggest remaining win. Reading the timer counter is still about a third of
  Renode's emulation thread.

The ArduPilot measurements show the same from the outside:
- Raising Renode's `PerformanceInMips` from 216 to 462 more than doubles the
  instructions it executes per virtual second.
- Its RTF stays at 0.30.
- Renode's executed instructions are nearly free; its time is in advancing
  virtual time ([docs/ardupilot.md](ardupilot.md#why-athrill-was-faster)).

**Athrill has that primitive.** Its clock is exact to the instruction, so a
device computes its counter from the clock at the moment of the access. The
access itself is:
- a direct function call;
- on the emulation thread;
- without locks or a time framework in between.

**How we read the 5-fold difference.** The table above gives two parts:
- the idle skip, a factor of 2.5 within Athrill;
- the per-access and per-event cost: even without the skip, Athrill is twice
  as fast as Renode.

This split is our reading. Renode's side was not profiled in this
comparison, so its per-access share on this workload is taken from the
ArduPilot notes above, not measured here.

## Limits

- **An idle function needs the clock skip.**
  - With `DEVICE_CONFIG_IDLE_FUNCTION`, the core halts instead of executing
    its idle loop.
  - The batches run only while the core runs, so with the skip off a halted
    core advances one clock per loop and ticks every device each time.
  - That is slower than leaving the idle loop to execute: 0.02 against 0.62
    above.
  - Athrill STM32 therefore ignores `DEVICE_CONFIG_IDLE_FUNCTION` when the
    skip is off: the idle loop executes in batches, with the same results.
    On ArduCopter, the first 20 virtual seconds then take 31 s instead of
    986 s, with identical output.

- **The idle function must really be idle.** If a firmware does work in its
  idle hook, halting there would change its behaviour. Leave
  `DEVICE_CONFIG_IDLE_FUNCTION` unset or `none` for such a firmware. The OS
  then runs as written, and the skip still applies at `WFI`.
- **No cycle accuracy.** One instruction is one clock. Pipeline,
  cache and bus timing are not modelled, so time-critical code sees an
  idealised CPU. This is the same for every run and every host.
- **Devices must report their events.** A device model that forgets to
  report an event would have it skipped over. This is a property of each
  model, not of the loop. Check it in the model's tests.

# Speed and fidelity

How Athrill STM32 is made fast, what its device models reproduce and leave out, and how a model can be made closer to
the chip. The measurements behind the figures here are in [ardupilot.md](ardupilot.md) and the
[README](../README.md#faster-a-block-jit-and-tick-skip).

## Our stance

Athrill STM32 aims at running **unmodified firmware fast and exactly the same on every run**:
- PX4, ArduPilot and Zephyr boot with their own drivers;
- the drivers detect the emulated chips;
- the firmware flies an aircraft in a physics simulator, faster than real time.

That is our acceptance level. Each device model reproduces what the firmware needs to reach it, and no more:
- the registers, their bits and their reset values, taken from the vendor's reference manual;
- the interrupts and DMA requests the firmware waits for;
- the time behaviour the firmware can observe through the registers, computed from Athrill's clock.

What a model leaves out, it leaves out on purpose, to stay light. A model does not reproduce, for example, the time
a bus transfer takes on the wire, or electrical behaviour.

**So:**
- **Use Athrill for** firmware logic, RTOS behaviour, flight stacks, and repeatable runs in CI.
- **Do not use it as** proof of driver timing on the real chip, or of board-level electrical, safety or flight behaviour.

## How it is made fast

Each of these is a design choice, not a trick for one firmware. None of them changes a result: the runs of the README
give bit-identical output with each of them on or off.

| Choice | What it means | Measured effect |
| --- | --- | --- |
| **One clock** | The whole board has one integer clock, and one instruction is one clock. There is no time framework to keep in step, and every model sees the same exact time. | Zephyr's `philosophers`, mostly asleep, with both emulators' console UART sending at once: the same 32 M instructions per 300 virtual s; Athrill runs at an RTF of about 430, Renode at about 10, with 69% of its CPU thread in its time framework, handing time between threads quantum by quantum. |
| **State computed when read** | A timer's counter, a cycle counter, a FIFO level or a UART's shift register is computed from the clock when the firmware reads it. Between accesses, nothing runs. | Not measured on its own. |
| **Time skip** | When the core waits for an interrupt (`WFI`, a branch to itself, or a named idle function), the clock jumps to the next device event. | ArduCopter's idle thread is a branch to itself; with the skip it executes 46.2 M of 216 M clocks per virtual second. Renode applies the same rule to a Thumb `b .`. |
| **Tick skip** | A model declares that its tick does nothing before its next event, and is not ticked until then. | 11% on the PX4 bench, 17% on ArduCopter, over the JIT alone. |
| **C only, in one thread** | The CPU and every device model are C, in one process and on one thread. A device access is one function call: no locks, no language boundary, no garbage collector. | On the same board and firmware, Athrill spends 0.11–0.18 s of host time per virtual second in its devices, Renode 0.74 s, while Athrill makes 1.3 times as many register accesses ([ardupilot.md](ardupilot.md)). |
| **A block JIT** | Each basic block is translated once into host code and chained. The block budget ends at the next device event, so the JIT stops exactly where the interpreter would. | About 1.6 times the interpreter on PX4 and ArduCopter. |

## Four models, in detail

These are the models where the comparisons with Renode show a difference (ArduCopter in [ardupilot.md](ardupilot.md), Zephyr in the USART row).
More models will be described here as needed.

| Model | Specification | What it reproduces | What it leaves out | What the firmware can see |
| --- | --- | --- | --- | --- |
| STM32F7 SPI | RM0410, chapter 35 | The registers, the RX FIFO and its level in `SR`, the DMA requests from `TXE`/`RXNE` while DMA is enabled | Transfer time: a write to `DR` exchanges the frame with the selected chip at once, so `TXE` is always set and `BSY` never | A driver that polls `SR` finds the transfer already done. The firmware may take another path than on the chip, with a different number of register accesses. |
| STM32F7 DMA | RM0410, chapter 8 | The streams, `NDTR`, circular mode, half and full transfer flags and interrupts; one item per request, read and written on the bus at `PAR` | Bus arbitration and transfer time: the items move as soon as the request is raised | A transfer completes in no virtual time. |
| STM32F7 USART | RM0410, chapter 34 | The registers, `TXE` and `TC` after the frame time at the `BRR` baud rate, reception at frame times, DMA requests | Line noise and errors. With `DEVICE_CONFIG_STM32_UART_TX_INSTANT 1`, the console UART also leaves out its transmit time | A firmware that polls its console waits for each character, as on the chip. Zephyr's `philosophers` on NUCLEO-F767ZI executes 798 M instructions in 30 virtual s on Athrill (RTF 0.63), against 3.5 M on Renode, whose USART completes at once (RTF about 10). With the option, Athrill executes the same 3.5 M and runs at an RTF of about 430. |
| ICM-20689 IMU | TDK datasheet, register map | The registers ArduPilot and PX4 use, the FIFO filled at the configured sample rate from Athrill's clock, its count and overflow | Sensor noise and filter dynamics of the chip; the values come from the attached source (a bench or the physics simulator) | The FIFO holds the samples of the elapsed virtual time when it is read. |

## Extending it

An emulator is made easy to extend in one of two ways:
- with a language and a framework in which models are easy to write;
- by keeping what a model must get right outside the hand-written code, and checking the rest.

Renode takes the first: its models are C#, behind a system bus and a time framework. Athrill takes the second, so
that a device access stays one function call and the clock stays one integer.

What a model would most often get wrong is kept as data:
- **The board** is its Devicetree: which parts it has, where, and on which interrupts and DMA streams. Adding a board
  adds no C code.
- **The registers** are generated from ST's SVD files: their offsets, fields, reset values and access. Errors in the
  SVD are corrected by patches that cite the reference manual. A binding that names a register the SVD lacks stops
  the generation.

What is left to write is the behaviour: what a model does when a bit is set or its time comes. It is C against a small
interface: six entries (validate, create, destroy, connect, reset, tick); a tick reports the model's next event.

The mistakes that remain are caught by tests:
- **Specification items:** each states one behaviour, cites the manual section it comes from, and has tests (in
  athrill-device, `spec/` and `docs/spec/`).
- **Differential tests:** tick skip on against off, and the JIT against the interpreter, compared after every step.
- **Bit-identical runs:** PX4, ArduPilot and Zephyr outputs compared byte for byte with published references.

**An example.** The console UART's option to send at once (`DEVICE_CONFIG_STM32_UART_TX_INSTANT`) changed 14 lines
of the UART model and added one test. On the same day it was checked that the default output stays bit-identical,
and that with the option Zephyr's `philosophers` executes nearly the same number of instructions as on Renode (3.48 M against 3.51 M in 30 virtual s).

## Making a model closer to the chip

The architecture lets a model become more faithful **without slowing down the rest of the board**:

- **A model is one Devicetree node.** The board is built from its Devicetree. A model is chosen by its node's
  `compatible`, and reads and writes its registers through the bindings, so a more faithful model replaces one node.
  The CPU and the other models do not change.
- **A model computes its state from the clock when it is read.** Athrill has one clock, exact to the instruction. A
  timer's counter, a FIFO level or a busy flag is a function of that clock. Making the time behaviour more faithful
  means changing that function, not adding work at every step.
- **A model reports its next event.** The CPU runs, in the interpreter or in the JIT, until the next event that some
  model reported, and stops there exactly. A model that needs to act at a certain clock reports that clock.
- **A model is ticked only when needed** (tick skip): a model declares that its tick does nothing before its next
  event, and is not called until then.

For example, an SPI transfer that takes its real time on the wire fits this design:
- the model would set `BSY` and clear `TXE` until the clock at which the frame ends;
- it would report that clock as its next event;
- the CPU would stop at that clock, and the model would complete the frame and raise its interrupt or DMA request.

The cost is paid only while a transfer is in flight, and only by that model. This is a possibility of the design, not
a model that exists today.

# Debugging firmware with GDB or LLDB

Athrill STM32 runs a GDB remote server when it is started with `-g<port>`.
GDB (`arm-none-eabi-gdb` or `gdb-multiarch`) and LLDB connect to it. You can
then set breakpoints, step, read the backtrace, and read and write registers
and memory of PX4, ArduPilot or any other firmware on the emulated board.
The server is in the Athrill core (toppers/athrill 3.1.0 or later). It is
built in by default when the core has it and the host has POSIX sockets
(`-DATHRILL_ENABLE_GDB_RSP=OFF` leaves it out).

Without `-g`, Athrill runs as before: the same fast execution path, the
same instruction counts and results.

## Start Athrill with the server

Add `-g<port>` to the usual command. Athrill listens on `127.0.0.1:<port>` and
stops before the first instruction until a debugger connects:

```bash
athrill3 -c1 -g1234 -m memory.txt -d device_config.txt -t -1 px4_fmu-v5_default.elf
```

## GDB

Use an Arm GDB with the same ELF. The GNU Arm toolchain's `arm-none-eabi-gdb`
runs on macOS; on Linux, `gdb-multiarch` works too:

```text
$ arm-none-eabi-gdb px4_fmu-v5_default.elf
(gdb) target remote :1234
(gdb) break ICM20689::RunImpl
(gdb) continue
Breakpoint 1, ICM20689::RunImpl (this=0x20006250) at ICM20689.cpp:146
(gdb) bt
#0  ICM20689::RunImpl (this=0x20006250) at .../ICM20689.cpp:146
#1  I2CSPIDriver<ICM20689>::Run (this=0x20006290) at .../i2c_spi_buses.h:345
#2  px4::WorkQueue::Run (this=0x200061a4) at .../WorkQueue.cpp:188
(gdb) next
(gdb) info registers r0 pc lr xpsr
(gdb) x/4wx $sp
(gdb) detach
```

VS Code's C/C++ debugger (`"MIMode": "gdb"`, `"miDebuggerServerAddress": "127.0.0.1:1234"`) connects the same way
(checked with the core's server on the RH850 target; not yet on STM32).

## LLDB

LLDB needs the register layout, which the server does not describe. The
target definition `tools/lldb/cortex_m_target_definition.py` gives it:

```text
$ lldb px4_fmu-v5_default.elf
(lldb) settings set plugin.process.gdb-remote.target-definition-file tools/lldb/cortex_m_target_definition.py
(lldb) gdb-remote 1234
(lldb) breakpoint set -n ICM20689::RunImpl
(lldb) continue
(lldb) bt
(lldb) next
(lldb) register read r0 pc lr
(lldb) memory read --count 16 $sp
(lldb) detach
```

## What the debugger sees

- Registers: r0-r12, sp (the current stack pointer), lr, pc and xPSR. This is
  the layout GDB uses for an M-profile ELF without a target description.
  xPSR combines APSR, IPSR and EPSR: the Thumb bit, and the IT bits from
  ITSTATE. Writing xPSR changes only the APSR flags (N Z C V Q, GE).
- Not yet visible: the FPU registers (s0-s31, fpscr), MSP/PSP and the special
  registers (PRIMASK, BASEPRI, CONTROL). Showing them needs a target
  description from the server.
- Breakpoints stop before the instruction at their address, in Thumb code of
  either width. A step runs one instruction (`stepi`); `next` and `step` are
  GDB's and LLDB's own line stepping on top of it.
- Debugging together with a Hakoniwa-connected run (`--hakoniwa`) is not
  supported yet (toppers/athrill#79).

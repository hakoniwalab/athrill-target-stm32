# ArduPilot Pixhawk 4 bench

This boots an unmodified ArduCopter Pixhawk 4 ELF. The seed F-RAM contains
`BRD_IO_ENABLE=0`, the normal ArduPilot setting for a board used without its
IO coprocessor. Without that setting ArduPilot tries to update the absent
IOMCU, reports `Failed to update IO firmware`, and deliberately remains in
`AP_BoardConfig::config_error()` before it creates the telemetry links.

Run it from the repository root:

```sh
examples/ardupilot-bench/run.sh /path/to/arducopter.elf 4320000000
python3 examples/ardupilot-bench/check-mavlink.py \
  examples/ardupilot-bench/mavlink.raw
```

The clock limit above is 20 virtual seconds at 216 MHz. `run.sh` copies
`fram-brd-io-disabled.bin` to the ignored writable `fram.bin` on the first
run. Remove `fram.bin` to return to that seed. `mavlink.raw` contains only
raw USART2 (SERIAL1/TELEM1) bytes; ordinary emulator diagnostics still go to
the terminal. This configuration neither opens nor uses a TCP listener.

The seed was provisioned through ArduPilot's `PARAM_SET` handling and then
run with the original ELF. It is data, not ArduPilot code or an emulation of
the IOMCU protocol.

For a workload with live deterministic bench sensors, a microSD card and a
GCS requesting the normal status/sensor streams, run:

```sh
examples/ardupilot-bench/run-full.sh /path/to/arducopter.elf 17280000000
examples/ardupilot-bench/inspect-mavlink.py \
  examples/ardupilot-bench/mavlink-full.raw
tools/list_fat.py examples/ardupilot-bench/sd.img
```

`run-full.sh` makes a local writable copy of the deterministic PX4 FAT32
image. The clock limit above is 80 virtual seconds. To create a Renode-format
trace of the first 20 million executed instruction PCs from the smaller
Round-2 workload, use `run-trace.sh`; the result is
`athrill-pc-trace.txt`. `tools/compare_pc_trace.py` compares it with another
trace while streaming both files and can resolve PCs with `--elf`.

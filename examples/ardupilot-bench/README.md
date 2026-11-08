# ArduPilot Pixhawk 4 bench

This boots an unmodified ArduCopter Pixhawk 4 ELF. The seed F-RAM contains
`BRD_IO_ENABLE=0`, the normal ArduPilot setting for a board used without its
IO coprocessor. Without that setting ArduPilot tries to update the absent
IOMCU, reports `Failed to update IO firmware`, and deliberately remains in
`AP_BoardConfig::config_error()` before it creates the telemetry links.

The board is Pixhawk 4's Devicetree from athrill-device
(`boards/pixhawk4/pixhawk4.dtb`): clone hakoniwalab/athrill-device next to
this repository, or point `ATHRILL_DEVICE_DIR` at it. The run scripts add
`DEVICE_CONFIG_DEVICE_TREE` to a copy of their configuration
(`device_config*-run.txt`, ignored by git).

The helper scripts need python3 with pymavlink
(`python3 -m pip install pymavlink`). Run them from the repository root:

```sh
examples/ardupilot-bench/run.sh /path/to/arducopter.elf 4320000000
python3 examples/ardupilot-bench/check-mavlink.py \
  examples/ardupilot-bench/mavlink.raw
```

The clock limit above is 20 virtual seconds at 216 MHz. Athrill then ends
with `EXIT for timeout` (exit status 1) and reports the run on stderr: with
the ArduCopter ELF of [docs/ardupilot.md](../../docs/ardupilot.md) it is
`steps=722036516` and `FINAL pc=0x08138a48`, the same on every run, and
`check-mavlink.py` reports 23 HEARTBEAT messages. Every run starts
from the seed: `run.sh` copies `fram-brd-io-disabled.bin` to the ignored,
writable `fram.bin`. `mavlink.raw` contains only
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

Every run of `run-full.sh` starts from the same state: the F-RAM seed and a
new, empty microSD image, made by `examples/px4-sih/make-sd-image.sh` (the
same bytes on every host). The clock limit above is 80 virtual seconds.

`run-trace.sh` writes the PCs of the first 20 million executed instructions,
in Renode's text format, to `athrill-pc-trace.txt`. It uses the plain boot
configuration of `run.sh`, without the sensor bench and the microSD card. `tools/compare_pc_trace.py` compares it with another
trace while streaming both files and can resolve PCs with `--elf`.

`renode/` holds the Renode side of the comparison in
[docs/ardupilot.md](../../docs/ardupilot.md): `bench-renode.sh` (three RTF
runs at each CPU setting), `measure-rtf.py` (the MAVLink measurement it uses)
and `trace.resc` (Renode's PC trace).

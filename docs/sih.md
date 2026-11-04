# PX4 SIH on Athrill

PX4's Simulation-In-Hardware (SIH) runs the vehicle model inside the flight
controller: the `simulator_sih` module computes the flight of a quadcopter
and gives PX4 its sensor data, and the board's sensor drivers are not
started. On a real Pixhawk this needs only the board and a ground station.
Here the board is Athrill: the stock `px4_fmu-v5_default` firmware (it
includes SIH) runs on the emulated STM32F765 of a Pixhawk 4, and
QGroundControl talks MAVLink to it over TCP.

## What the board provides

| Part | Model | Use in SIH |
|------|-------|------------|
| F-RAM FM25V02A on SPI2 (chip select PF5) | `src/device/memory/fm25v02.c` | the parameters (`/fs/mtd_params`), kept in a host file |
| TELEM1 = USART2 (`/dev/ttyS1`), receive DMA (DMA1 stream 5) | `stm32_uart.c`, `stm32_dma.c` | MAVLink |
| UART line to a TCP client | `stm32_uart_tcp.c` | QGroundControl, pymavlink |
| microSD card on SDMMC1 (4-bit, DMA2 stream 6) | `stm32_sdmmc.c`, `src/device/memory/sdcard.c` | flight logs (ulog), the data manager, the parameter backup, kept in a host disk image |
| UART7 | console | NSH |

Not present, and not needed for SIH: the PX4IO co-processor (its start
fails; SIH outputs go to the simulator, not to PWM), USB.

## Device configuration

```
DEVICE_CPU_FREQ 216
DEBUG_FUNC_ENABLE_SKIP_CLOCK 1
DEVICE_CONFIG_STM32_BOOT_ADDR 0x08008000
DISABLE_SYMBOL_UNDERSCORE 1
DEVICE_CONFIG_STM32_BOARD fmu-v5
DEVICE_CONFIG_STM32_CONSOLE_UART 7
DEVICE_CONFIG_STM32_FRAM_FILE fram.bin
DEVICE_CONFIG_STM32_UART2_TCP_PORT 5760
DEVICE_CONFIG_STM32_SDCARD_IMAGE sd.img
```

- `DEVICE_CONFIG_STM32_FRAM_FILE`: the host file of the F-RAM contents
  (32 KB, created when missing). Without it the parameters are lost when
  Athrill stops.
- `DEVICE_CONFIG_STM32_UART<n>_TCP_PORT`: UART `n` (1 to 8) is connected
  to one TCP client on `127.0.0.1:<port>`; only the local host can
  connect. Without a client its output is dropped.
- `DEVICE_CONFIG_STM32_SDCARD_IMAGE`: the microSD card, a raw disk image
  (FAT32 without a partition table; the size is used in 512 KiB steps).
  Without it the slot is empty: PX4 reports "Missing FMU SD Card", and
  QGroundControl "Storage full: 0 MiB free".
- `DEVICE_CONFIG_STM32_UART_RX_FILE`: characters typed into the console
  (NSH commands), as in the other runs.

## The SD card image

On macOS, a 1 GB FAT32 image:

```
mkfile -n 1g sd.img
DISK=$(hdiutil attach -imagekey diskimage-class=CRawDiskImage -nomount sd.img | awk '{print $1}')
newfs_msdos -F 32 -v PX4SD "$DISK"
hdiutil detach "$DISK"
```

After a run, `hdiutil attach -readonly -imagekey
diskimage-class=CRawDiskImage sd.img` mounts it as `/Volumes/PX4SD`, with
the logs in `log/sess*/` for Flight Review or PlotJuggler. Do not keep it
mounted while Athrill runs.

## First boot: select the SIH airframe

The parameters start empty. Give the console these commands once (an RX
file with them, or the debugger's serial input):

```
sleep 4
param set SYS_AUTOSTART 1100
param save
```

1100 is the SIH quadcopter X (`1100_rc_quad_x_sih.hil`): it sets
`SYS_HITL 2`, which starts `simulator_sih` and the simulated barometer,
magnetometer and GPS instead of the board's sensor drivers. It is stored
in the F-RAM file and takes effect at the next start of Athrill. Without
an SD card, also `param set SYS_DM_BACKEND 1` (the data manager in RAM).

## Run and fly

```
athrill3 -c1 -m memory.txt -d device_config.txt -t 60000000000 px4_fmu-v5_default.elf
```

PX4 boots with `Loading airframe: /etc/init.d/airframes/1100_rc_quad_x_sih.hil`
and starts MAVLink on `/dev/ttyS1` (57600 baud, 2000 B/s). Connect
QGroundControl with a TCP link to `127.0.0.1:5760` (Application Settings,
Comm Links), or pymavlink with `tcp:127.0.0.1:5760`. The vehicle stands at
PX4's SIH home (Zurich).

## Results (2026-11-04, Apple M-series host)

- From NSH (`commander takeoff`) and over MAVLink (arm, then
  `MAV_CMD_NAV_TAKEOFF`, both acknowledged) the quadcopter takes off to
  the default 2.5 m and holds 2.49 to 2.51 m.
- RTF 1.25 to 1.28 while hovering without the SD card, 1.19 with it
  (logging about 65 KiB/s), measured from MAVLink `time_boot_ms` against
  wall-clock time.
- With the SD card the log of a takeoff and hover (40 s, 90 topics, no
  dropouts) reads back with pyulog on the host.
- PX4's load monitor reports `sih low on stack! (184 bytes left)`; the
  stack size is the firmware's own, as on the board.

## Lessons

- The SDMMC must end a data transfer only after the time the bytes take
  on the bus: NuttX arms its wait for the end after it sends the command,
  and an end signalled at once would be lost.
- A UART received by DMA never has its RDR read, so the receiver must
  restart its frame timing from the arrival of each character itself;
  otherwise a backlog arrives at every device tick and overruns NuttX's
  32-byte circular buffer (bytes lost, PX4 MAVLink `rx` near 0).
- PX4's `mavlink status` shows rates over the last moments; to see whether
  a link receives, keep the client sending (heartbeats) while asking.

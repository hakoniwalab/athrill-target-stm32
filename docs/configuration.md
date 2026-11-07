# STM32 target configuration

This page covers parameters read by the Athrill STM32 target. Command-line options, the memory configuration, the
device-configuration grammar, and common parameters such as clock skipping and host functions are in the
[Athrill core configuration reference](https://github.com/toppers/athrill/blob/master/docs/configuration.md).

The target models one STM32F765 core. A typical PX4 run explicitly sets `DEVICE_CPU_FREQ 216`, maps the PX4 vector
table with `DEVICE_CONFIG_STM32_BOOT_ADDR 0x08008000`, and selects the `fmu-v5` board. Paths are resolved from the
process working directory.

## Devicetree-built devices

`DEVICE_CONFIG_DEVICE_TREE path/to/board.dtb` selects the board: its Devicetree (athrill-device `boards/<board>/*.dtb`). Boards are built only from the Devicetree.
The path is resolved from Athrill's working directory. Without it, no board
devices are built; `DEVICE_CONFIG_STM32_BOARD` without a Devicetree is an
error.

Optional model plugins are explicit, zero-based mappings,
`DEVICE_CONFIG_DEVICE_PLUGIN_<n>` with `n` from 0. Enumeration stops
at the first absent index, and a plugin cannot override a built-in compatible:

```text
DEVICE_CONFIG_DEVICE_PLUGIN_0 hakoniwa%2Csensor-device=/opt/athrill/libhako_sensor_device.so
```

`%2C` represents the compatible's comma because the unchanged Athrill
configuration tokenizer treats a literal comma as a field separator.

Host-only values such as the F-RAM and SD image paths remain in
`device_config.txt`. At startup they are logged on `DEVICE_TREE_HOST` lines
with the exact DTB SHA-256.

## Common core parameter used by the target

| Key | STM32 behavior | Default and dependency | Example line |
| --- | --- | --- | --- |
| `DEVICE_CPU_FREQ` | MHz used by STM32 peripheral timing, the SDMMC clock, sensor timing, and PWM conversion, as well as by the core. Use a positive decimal value. | The target's peripheral fallbacks are `216`, while the core fallback is `100`; set the key explicitly to keep them consistent. | `DEVICE_CPU_FREQ 216` |

Other common keys frequently used here are `DEBUG_FUNC_ENABLE_SKIP_CLOCK`, `DEVICE_CONFIG_IDLE_FUNCTION`,
`DEVICE_CONFIG_SPIN_HALT`, `DISABLE_SYMBOL_UNDERSCORE`, and the host-function keys. STM32 defaults the core idle
function to `up_idle`; `DEVICE_CONFIG_IDLE_FUNCTION none` disables that optimization. Its branch-to-self detector
supports the generic spin-halt key. See the core page for values, dependencies, and examples.

## Boot and CPU execution

| Key | Meaning and accepted values | Default and dependency | Example line |
| --- | --- | --- | --- |
| `DEVICE_CONFIG_STM32_BOOT_ADDR` | Address of the vector table. Hexadecimal is preferred; decimal is also accepted. On the first clock after load, word 0 becomes MSP and word 1 becomes PC (bit 0 cleared), and VTOR is set to this address. | `0x08000000`. The memory map must cover both words. For an application linked after a bootloader, use its application vector address. | `DEVICE_CONFIG_STM32_BOOT_ADDR 0x08008000` |
| `DEVICE_CONFIG_ARM_EXEC_MODE` | `fast` uses the target's checked fast Thumb executors where available; `strict` routes all Thumb instructions through the generic Arm pseudo-code. Any other string warns and selects `fast`. | `fast`. | `DEVICE_CONFIG_ARM_EXEC_MODE strict` |
| `DEVICE_CONFIG_ARM_FP_MODE` | `fast` uses host floating-point operations in cases that give the required bits and otherwise falls back; `strict` uses the generic Arm floating-point pseudo-code. Any other string warns and selects `fast`. | `fast`. | `DEVICE_CONFIG_ARM_FP_MODE strict` |

## Board and sensor source

| Key | Meaning and accepted values | Default and dependency | Example line |
| --- | --- | --- | --- |
| `DEVICE_CONFIG_STM32_BOARD` | The SoC profile: `fmu-v5` (STM32F7) or `fmu-v6c` (STM32H7) for the boot and probe registers. The board itself (its devices and wiring) comes only from `DEVICE_CONFIG_DEVICE_TREE`; a configuration with this key and no Devicetree stops at start with an error. | Absent: STM32F7 profile. | `DEVICE_CONFIG_STM32_BOARD fmu-v6c` |
| `DEVICE_CONFIG_STM32_FRAM_FILE` | Host backing file for the FM25V02A F-RAM. The target creates it if needed, reads existing bytes, and sizes it to the 32 KiB chip. | Absent: the selected board's F-RAM starts zeroed and is not persisted. Ignored unless `fmu-v5` or `fmu-v6c` is selected. | `DEVICE_CONFIG_STM32_FRAM_FILE fram.bin` |
| `DEVICE_CONFIG_STM32_SDCARD_IMAGE` | Writable host image for the selected board's SDHC card (SDMMC1 on v5, SDMMC2 with IDMA on v6C). It must exist and be at least 512 KiB; usable size is rounded down to a 512 KiB multiple. | Absent or unusable: empty card slot. Ignored unless `fmu-v5` or `fmu-v6c` is selected. | `DEVICE_CONFIG_STM32_SDCARD_IMAGE sd.img` |
| `DEVICE_CONFIG_STM32_ACTUATOR_FILE` | Text trace of PWM output changes after the selected board's ordered timer channels have passed through the shared waveform-to-actuator path. Each line has the output, timer, channel, pulse width in microseconds, and normalized command. | Absent: no trace file; actuator delivery is unchanged. The file is truncated when the board is attached. | `DEVICE_CONFIG_STM32_ACTUATOR_FILE actuators.txt` |
| `DEVICE_CONFIG_STM32_SENSOR_DEVICE` | Hex base at which the target looks for the Hakoniwa sensor external device. The dynamic map must start exactly there and return the `HAKO` ID. | `0x60000000`. Requires a matching `DEV` line in the memory configuration. | `DEVICE_CONFIG_STM32_SENSOR_DEVICE 0x60000000` |
| `DEVICE_CONFIG_STM32_SENSOR_BENCH` | Nonzero supplies deterministic, fixed-seed measurements for a stationary, level vehicle at Tokyo Station when no sensor device is present. | `0`; without a device or bench, the chips return constant inert values and no GPS. The samples reach firmware through the chips attached by `fmu-v5`. | `DEVICE_CONFIG_STM32_SENSOR_BENCH 1` |

## UARTs

STM32 UART numbers are 1 through 8. The console connects one UART to Athrill's terminal and optional files; a board
or TCP link can attach a chip to a non-console UART.

| Key | Meaning and accepted values | Default and dependency | Example line |
| --- | --- | --- | --- |
| `DEVICE_CONFIG_STM32_CONSOLE_UART` | Decimal UART number used for debugger/terminal input and output. A value outside 1–8 leaves no console UART. | `3` (USART3). | `DEVICE_CONFIG_STM32_CONSOLE_UART 7` |
| `DEVICE_CONFIG_STM32_UART_RX_FILE` | Binary input file for the selected console UART. Bytes arrive at UART frame times while the guest receiver is listening; data appended after EOF is polled. | Absent: no file input. Terminal input can still be used in interactive mode. | `DEVICE_CONFIG_STM32_UART_RX_FILE console-in.bin` |
| `DEVICE_CONFIG_STM32_UART_RX_DELAY_MS` | Virtual milliseconds after reset before bytes from `DEVICE_CONFIG_STM32_UART_RX_FILE` start arriving. This lets deterministic boot checks wait for a shell or telemetry port to open. | `0`; file input starts immediately. | `DEVICE_CONFIG_STM32_UART_RX_DELAY_MS 1500` |
| `DEVICE_CONFIG_STM32_UART_TX_FILE` | Binary output file for bytes transmitted by the selected console UART, without Athrill's own console text. | Absent: no capture. When present, the file is opened with truncation at reset. | `DEVICE_CONFIG_STM32_UART_TX_FILE console-out.bin` |
| `DEVICE_CONFIG_STM32_UART_TX_DMA` | Nonzero enables the target's memory-to-UART-TDR DMA path. | `0`. Enable it for firmware whose UART output uses DMA; ordinary register transmission is always modeled. | `DEVICE_CONFIG_STM32_UART_TX_DMA 1` |
| `DEVICE_CONFIG_STM32_UART<n>_TCP_PORT` | For `n` from 1 through 8, a decimal TCP port from 1 through 65535. Athrill listens on `127.0.0.1`, accepts one client at a time, receives through that UART, and sends its output to the client. | Absent or zero: no listener for that UART. A bind/listen failure also disables it. The link is attached after the board, so choose a UART not occupied by a board chip. | `DEVICE_CONFIG_STM32_UART2_TCP_PORT 5760` |

## Tracing and sampling

| Key | Meaning and accepted values | Default and dependency | Example line |
| --- | --- | --- | --- |
| `DEVICE_CONFIG_ARM_PC_TRACE_FILE` | File to which one hexadecimal instruction PC is written per line. | No trace. Must be paired with a nonzero count; the file is truncated when opened. | `DEVICE_CONFIG_ARM_PC_TRACE_FILE athrill-pc-trace.txt` |
| `DEVICE_CONFIG_ARM_PC_TRACE_COUNT` | Decimal number of instruction PCs to record. The trace closes after exactly this many records. | No trace. Must be paired with a file path and must be nonzero. | `DEVICE_CONFIG_ARM_PC_TRACE_COUNT 20000000` |
| `DEVICE_CONFIG_ARM_PC_TRACE_SKIP` | Decimal number of instruction PCs to skip before recording. | `0`. Requires an enabled PC trace; useful for a narrow trace around a late divergence without writing the boot prefix. | `DEVICE_CONFIG_ARM_PC_TRACE_SKIP 3000000000` |
| `DEVICE_CONFIG_ARM_PC_TRACE_CLOCKS` | Nonzero prefixes every recorded PC with its decimal virtual clock. | `0` (one hexadecimal PC per line). Requires an enabled PC trace. | `DEVICE_CONFIG_ARM_PC_TRACE_CLOCKS 1` |
| `DEVICE_CONFIG_STM32_PROBE_TRACE` | Nonzero reports on stderr every register access that still reaches the target's peripheral probe instead of a Devicetree model: one line per 1 KiB block and direction, `PROBE_TRACE`, `R` or `W`, the owner, the block address and `pc=` with the PC of the first access. The owner is the target model behind the address (`uart`, `dma`, `dmamux`, `i2c`, `sdmmc`, `spi`, `h7spi`) or the probe's own storage (`h7boot`, `register-file`). A model moved to athrill-device must no longer appear. | `0` (no trace). Host-side only: the instruction count is unchanged. | `DEVICE_CONFIG_STM32_PROBE_TRACE 1` |
| `DEBUG_PC_SAMPLE_START` | Decimal instruction-clock threshold after which the target records a PC sample every 1000 executed instruction clocks. PCs are accumulated in 64-byte blocks and printed at exit with the final CPU state. | Absent: sampling is disabled, although the final state and empty sample summary are still printed. | `DEBUG_PC_SAMPLE_START 1000000` |

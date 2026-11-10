# Zephyr samples on Athrill STM32

Zephyr applications built for Zephyr's own `nucleo_f767zi` (STM32F767) and
`nucleo_h743zi` (STM32H743) boards run on Athrill unchanged. The Athrill
board, athrill-device's `boards/nucleo-f767zi` and `boards/nucleo-h743zi`,
includes Zephyr's board description itself, so the firmware and the
emulated board come from the same Devicetree.

Checked with three of Zephyr's samples on both boards:

| Sample | What `check.sh` requires |
| --- | --- |
| `hello_world` | `Hello World! nucleo_f767zi/stm32f767xx` (or the H743 line) |
| `synchronization` | two threads take turns through semaphores, one greeting every 0.5 s: 9 alternating greetings in 5 virtual seconds |
| `philosophers` | six threads of different priorities share forks through mutexes: every philosopher eats at least 10 times in 30 virtual seconds |

## Run

The release has the six ELFs (`<sample>-<board>.elf`). To run one:

```sh
examples/zephyr/run.sh nucleo_f767zi hello_world-nucleo_f767zi.elf 1
```

The last argument is a limit in virtual seconds; without it the
application runs until Ctrl+C. The console is USART3, Athrill's console.
To check all six:

```sh
examples/zephyr/check.sh <directory with the ELFs>
```

Both scripts take the board DTBs from athrill-device next to this
repository (or `ATHRILL_DEVICE_DIR`) and `build/athrill3` (or the last
argument).

## Compared with Renode

`philosophers` built for `nucleo_f767zi` also runs, unchanged, on Renode's stock STM32F746 platform. Measured on an
Apple M2 Pro (2026-11-11), Athrill with its JIT and tick skip:

| | Virtual time | Wall time | RTF | Instructions |
| --- | ---: | ---: | ---: | ---: |
| Athrill, default | 30 s | 47.9 s | 0.63 | 798 M |
| Athrill, `DEVICE_CONFIG_STM32_UART_TX_INSTANT 1` | 300 s | 0.70 s | about 430 | 32.2 M |
| Renode, unthrottled | 300 s | 29.8 s after a start-up of 2.7 s | about 10 | 32.2 M |

- **By default Athrill's USART takes the frame time of each character** at 115200 baud, as the chip does, and Zephyr
  polls its console for each one. Renode's USART completes at once, so Renode runs far fewer instructions.
- **With the option, Athrill's console UART sends at once too,** and the two emulators execute the same instructions.
  The consoles then match except for the order of one update in 753, a timing difference.
- **Where the time goes:**
  - Renode's emulation thread spends 69% in its time framework, handing time between threads quantum by quantum.
  - Athrill spends a third of its time writing the console to the host, and jumps its clock from event to event.

To run both, with `build-fast/athrill3` built as at the end of the main README's *Build from source*:

```sh
printf 'DEVICE_CONFIG_STM32_JIT 1\nDEVICE_CONFIG_DEVICE_TICK_SKIP 1\nDEVICE_CONFIG_STM32_UART_TX_INSTANT 1\n' > /tmp/athrill-zephyr.txt
/usr/bin/time -p examples/zephyr/run.sh nucleo_f767zi philosophers-nucleo_f767zi.elf 300 build-fast/athrill3 /tmp/athrill-zephyr.txt > /tmp/athrill-console.txt
examples/zephyr/renode/run-renode.sh ~/renode/renode philosophers-nucleo_f767zi.elf 300
```

`~/renode/renode` is a Renode launcher; ArduPilot's performance branch was used here (see
[docs/ardupilot.md](../../docs/ardupilot.md#renode)). Leave out the last line of `/tmp/athrill-zephyr.txt` for the
default.

## Build the samples

```sh
examples/zephyr/build.sh
```

The script needs Docker, git and curl. It fetches Zephyr and the modules it
needs at the revisions of `revisions.txt`. Zephyr's revision is the one the
athrill-device boards include. It downloads the Zephyr SDK's
`arm-zephyr-eabi` compiler and checks it against the SDK's `sha256.sum`, then
builds the samples unchanged in a small Ubuntu image (`Dockerfile`). The
ELFs go to `work/elf/`.

## Your own application

Build it for `nucleo_f767zi` or `nucleo_h743zi` as for the board, then pass
the ELF to `run.sh`. The board files list what Athrill does not model:
DAC1 and the Ethernet MAC are switched off, and on H743 LPUART1 only
answers its start-up. An application that needs those does not run.
The modelled peripherals are those of the Pixhawk boards: GPIO, EXTI, USART,
SPI, I2C, timers and PWM, DMA, SDMMC and the start-up register blocks.

## Licenses of the ELFs

The ELFs contain Zephyr and CMSIS (Apache-2.0), STM32Cube LL/HAL drivers
from hal_stm32 (BSD-3-Clause), and picolibc and libgcc from the Zephyr
SDK (BSD-style licenses, and the GCC Runtime Library Exception). No ST
binary blob is used. `revisions.txt` and `build.sh` identify the sources.

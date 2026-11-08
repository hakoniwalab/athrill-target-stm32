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

# examples/ardupilot-hakoniwa

> **Not working since the boards built by C code were removed (2026-11-07).** This example still selects its board with `DEVICE_CONFIG_STM32_BOARD` and loads the Hakoniwa sensor device through `memory.txt` (`DEV`); boards now come only from their Devicetree in athrill-device (`boards/<board>/*.dtb`, sensor device as `DEVICE_CONFIG_DEVICE_PLUGIN_0`). It is to be replaced by the recipes (hakoniwalab/athrill-device#30); the last working state is the tag `c-path-final`.

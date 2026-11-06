# Host functions

This MIT-licensed example replaces the Arm firmware implementations of
`memset`, `memcpy`, and `memmove` with host C. Guest addresses are never cast
to host pointers: every complete range is checked by Athrill first. A device
range, MPU refusal, ROM write, or range crossing a memory-region boundary
returns `FALSE`, and Athrill executes the guest function normally.

Build it either with the STM32 target or by itself:

```sh
cmake -S examples/host-functions -B build/host-functions \
  -DATHRILL_SOURCE_DIR=/path/to/athrill
cmake --build build/host-functions
```

Add the resulting absolute library path and these mappings to the PX4 device
configuration (`examples/px4-bench/device_config.txt`,
`examples/px4-sih/device_config.txt`, or
`examples/px4-hakoniwa/device_config.txt`):

```text
DEVICE_CONFIG_HOST_FUNCTION_LIBRARY /absolute/path/libathrill_host_functions.dylib
DEVICE_CONFIG_HOST_FUNCTION memset athrill_host_memset
DEVICE_CONFIG_HOST_FUNCTION memcpy athrill_host_memcpy
DEVICE_CONFIG_HOST_FUNCTION memmove athrill_host_memmove
```

Use `.so` instead of `.dylib` on Linux. The same four lines apply to
ArduCopter's `examples/ardupilot-bench/device_config.txt` and
`examples/ardupilot-hakoniwa/device_config.txt`.

At startup Athrill prints every resolved firmware entry address. At exit it
prints handled and refused call counts for each mapping.

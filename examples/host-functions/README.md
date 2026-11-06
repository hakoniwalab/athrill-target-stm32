# Host functions

This MIT-licensed example replaces the Arm firmware implementations of
`memset`, `memcpy`, and `memmove` with host C. Guest addresses are never cast
to host pointers: every complete range is checked by Athrill first. A device
range, MPU refusal, ROM write, or range crossing a memory-region boundary
returns `FALSE`, and Athrill executes the guest function normally.

A hooked call counts as **one instruction (one clock)**, whatever the function does: the run stays
deterministic, but the instruction count and the timing differ from a run without host functions (the
exact-count checks of `examples/px4-bench` and `examples/px4-sih` apply without them).

Build it from this repository, with the Athrill core checked out next to this repository (`../athrill`, as for the target):

```sh
cmake -S examples/host-functions -B build/host-functions
cmake --build build/host-functions
```

Then add the library and the mappings to the device configuration, here PX4 on Hakoniwa
(`examples/px4-bench/device_config.txt` and `examples/px4-sih/device_config.txt` take the same lines):

```sh
cat >> examples/px4-hakoniwa/device_config.txt <<EOF
DEVICE_CONFIG_HOST_FUNCTION_LIBRARY $(pwd)/build/host-functions/libathrill_host_functions.dylib
DEVICE_CONFIG_HOST_FUNCTION memset athrill_host_memset
DEVICE_CONFIG_HOST_FUNCTION memcpy athrill_host_memcpy
DEVICE_CONFIG_HOST_FUNCTION memmove athrill_host_memmove
EOF
```

Use `.so` instead of `.dylib` on Linux. The same four lines apply to
ArduCopter's `examples/ardupilot-bench/device_config.txt` and
`examples/ardupilot-hakoniwa/device_config.txt`.

At startup Athrill prints every resolved firmware entry address. At exit it
prints handled and refused call counts for each mapping.

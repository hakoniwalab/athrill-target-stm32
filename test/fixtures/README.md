# Test fixtures

C-path chip models kept only for test_v6c_sensors, which calls them directly
(BMI055, ICM-42688-P, MS5611), with the chip-operation types the target's SPI
and I2C controllers used. They are not built into athrill3; the boards come
from the Devicetree, and the controllers and chips are athrill-device models.

They go when the controller tests drive the athrill-device models instead
(hakoniwalab/athrill-target-stm32#44).

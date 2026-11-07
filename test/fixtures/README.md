# Test fixtures

Models kept only as bus counterparts for the controller tests: the GPIO and
EXTI models, the sensor chips and the F-RAM. They are not built into athrill3;
the boards come from the Devicetree (athrill-device).

They go when the controller tests drive the athrill-device models instead
(hakoniwalab/athrill-target-stm32#44).

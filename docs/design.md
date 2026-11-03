# Design: Athrill STM32 target for a lightweight PX4 PIL

This document records the design decisions for the Athrill STM32 target.

## 1. Goal

A lightweight Processor-in-the-Loop (PIL) for flight-controller firmware:
PX4 built for a Pixhawk board runs on Athrill, which emulates the board's
STM32 microcontroller, and flies a vehicle simulated by Hakoniwa (physics and
sensors, for example in MuJoCo) in lockstep.

"Lightweight" is the deciding criterion. Athrill models what the firmware
needs to run and to be checked, not the whole chip:

- The firmware should run as built for the real board, with its own drivers.
- Where modelling real hardware costs much and adds little to what is
  checked, a virtual device (an Athrill-only device with a small driver) is
  allowed, as in the RISC-V target's PX4 PIL. Each such choice is written
  down here with its reason.

## 2. Decisions

| # | Decision | Reason |
|---|---|---|
| 1 | The first board is Pixhawk 4 (PX4 board `px4_fmu-v5`, STM32F765, Cortex-M7) | PX4 and ArduPilot both support it; its clock tree and DMA are simpler than the STM32H7's. Pixhawk 6X (`px4_fmu-v6x`, STM32H753, also Cortex-M7) follows with the same CPU and different peripherals |
| 2 | PX4 first; ArduPilot (ChibiOS) after PX4 flies | One firmware at a time; PX4 on NuttX is what the RISC-V target already runs |
| 3 | Sensors: emulate the real sensor chips on SPI and I2C where practical, so that PX4's own drivers run unchanged; fall back to a virtual device where a chip is costly to model | The real chips keep the firmware as built and check the driver path too. The virtual device keeps the PIL lightweight where needed |
| 4 | Motor outputs are taken from the timer peripherals' PWM outputs (pulse width from the compare registers), not from an extra communication channel | The output is what the board would drive to the ESCs; no added channel changes what is observed |
| 5 | Hakoniwa link: the same as the RISC-V target's PX4 PIL. Sensor samples come from the PDU channels `hil_sensor` and `hil_gps` (`hako_mavlink_msgs/HakoHilSensor`, `HakoHilGps`), the motor outputs go to `hil_actuator` (`HakoHilActuatorControls`), and both sides move on when the other side's data arrives | One link for both targets; the order is fixed by the data, not by wall-clock time |
| 6 | The ground station connects over a UART (TELEM) as a TCP client, as in the RISC-V target; USB comes later, if at all | A UART is a small model; USB OTG is large |
| 7 | Athrill's execution engine follows the RISC-V target: decode cache, batched execution, straight-line blocks, and the rule that any speed-up gives byte-identical output to one-instruction-at-a-time execution | Speed without changing behaviour; the rule makes each step checkable |
| 8 | No code from other emulators (QEMU, Renode and others). They may be used as references for behaviour and for comparing traces only | Athrill's own implementation and licence |
| 9 | ArduPilot is GPL: Athrill runs ArduPilot binaries, and no ArduPilot code or header enters this repository | Keeps this repository MIT |

## 3. What Athrill models

### 3.1 CPU (Armv7-M, Cortex-M7)

- Thumb-2 instruction set, including the hardware divide and DSP instructions
  that the compilers emit for Cortex-M7.
- Floating point FPv5 with double precision (the STM32F765's FPU).
- Exception model: reset, NVIC with priorities, SysTick, PendSV and SVCall,
  exception entry and return with the stacked frame, lazy floating-point
  stacking as configured by the firmware.
- MPU (PMSAv7) as far as NuttX and PX4 configure it.
- Caches and the TCMs: accepted and reported as present; no cache effects
  are modelled.

### 3.2 Peripherals needed to boot

The firmware waits on ready bits and status flags; these must answer as the
hardware does. In the order the firmware meets them:

| Peripheral | Needed for |
|---|---|
| RCC, PWR, FLASH interface | clock setup (PLL lock and switch), voltage scaling, wait states |
| GPIO | pin setup; board version and LEDs |
| USART | NSH console and the ground station (TELEM) |
| Timers (TIM) | PX4's high-resolution timer (HRT), PWM outputs |
| DMA | serial and SPI transfers used by PX4's drivers |
| SPI, I2C | sensor chips (3.3) and parameter storage |
| ADC | board power and version sensing, if PX4 requires it to boot |
| Watchdog | as far as PX4 enables it |

Later or never: USB OTG, SDMMC (logging), CAN, DShot.

### 3.3 Sensors

The sensor chips of the FMUv5 board definition in PX4 are listed and modelled
one by one, at the register level their drivers use (identification, setup,
data-ready, data registers or FIFO). The samples come from `hil_sensor` and
`hil_gps` (5). The GPS receiver on its UART follows the same rule: emulate it
where practical, otherwise a virtual device.

### 3.4 Motor outputs

The timer channels that PX4's FMUv5 board definition uses for the main
outputs: Athrill reads the period and pulse width and writes the outputs to
`hil_actuator` in the same form as PX4 SITL's HIL_ACTUATOR_CONTROLS.

## 4. Stages

| Stage | Content | Done when |
|---|---|---|
| 0 | Survey | Existing STM32 emulation (Renode and others) and Arm's instruction set documentation and its terms are checked; what may be used is written down here |
| 1 | CPU and the peripherals to boot | PX4 `px4_fmu-v5_default`, as built, boots to the NSH prompt |
| 2 | Sensors, PWM outputs and the Hakoniwa link | Hakoniwa's x500 takes off and hovers with PX4 |
| 3 | ArduPilot | ArduPilot's Pixhawk 4 firmware boots on ChibiOS, then flies the same x500 |
| 4 | Pixhawk 6X | `px4_fmu-v6x` boots and flies |

Each stage keeps the rule of 2-7: every speed-up is checked against
one-instruction-at-a-time execution.

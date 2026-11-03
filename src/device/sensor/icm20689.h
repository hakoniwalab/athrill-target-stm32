/*
 * The TDK InvenSense ICM-20689 6-axis IMU on SPI (datasheet DS-000143):
 * the register file (WHO_AM_I 0x98, the configuration registers read back
 * as written, DEVICE_RESET, SIGNAL_PATH_RESET, FIFO_RST), the data
 * registers, the FIFO and the data-ready pin.
 *
 * Samples come from the sensor source at the configured rates, scaled by
 * the configured full-scale ranges and turned into the chip's own axes
 * (x forward, y left, z up, and the board's mounting rotation). The FIFO is
 * filled at the gyro rate (8 kHz with the DLPF bypassed, else 1 kHz /
 * (1 + SMPLRT_DIV)) with accel and gyro records while FIFO_EN asks for
 * them; the accel changes at half that rate, as PX4's driver expects of the
 * chip. Each sample pulses the data-ready pin when DATA_RDY_INT_EN is set.
 * Lightweight: no self-test, no motion detection, no DLPF.
 */
#ifndef ATHRILL_STM32_ICM20689_H
#define ATHRILL_STM32_ICM20689_H

#include "std_types.h"
#include "cpu.h"
#include "std_device_ops.h"
#include "peripheral/stm32_spi.h"

#define ICM20689_FIFO_BYTES	512U

/* The chip's mounting on the board, as PX4's rotation of its samples (-R). */
typedef enum {
	ICM20689_ROTATION_NONE = 0,
	ICM20689_ROTATION_YAW_90 = 2,
} Icm20689RotationType;

typedef struct {
	uint8 regs[128];
	Icm20689RotationType rotation;
	uint32 drdy_port;	/* the data-ready pin (STM32_GPIO_PORT) */
	uint32 drdy_pin;
	uint64 clocks_per_sample;	/* CPU clocks per FIFO sample at 8 kHz; set by the board */
	/* SPI transaction */
	std_bool started;
	std_bool reading;
	uint8 address;
	/* FIFO: samples taken since fifo_start, of which fifo_read read out (12 bytes each) */
	uint64 fifo_start;
	uint64 fifo_taken;
	uint64 fifo_read_bytes;
	std_bool fifo_overflow;
	uint64 next_sample;	/* the clock of the next data-ready pulse */
} Icm20689Type;

extern const Stm32SpiChipOps icm20689_ops;

/* cpu_hz: the CPU clock (DEVICE_CPU_FREQ). */
void icm20689_reset(Icm20689Type *chip, uint64 cpu_hz, Icm20689RotationType rotation, uint32 drdy_port, uint32 drdy_pin);
/* The data-ready pulses up to the device clock; the clocks to the next one. */
void icm20689_tick(Icm20689Type *chip, DeviceClockType *clock);

#endif

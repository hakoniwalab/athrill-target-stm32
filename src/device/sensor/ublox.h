/*
 * A u-blox M8 GNSS receiver on a UART, speaking UBX (u-blox 8 / M8
 * receiver description, protocol 18): it acknowledges the configuration
 * messages (CFG-*) with ACK-ACK, answers CFG-VALSET (protocol 27 and
 * later) with ACK-NAK so that the host keeps to the M8 protocol, answers
 * the MON-VER poll as an M8 (hardware version 00080000), takes the
 * message rates (CFG-MSG) and the measurement period (CFG-RATE), and sends
 * NAV-PVT and NAV-DOP at those rates from the sensor source's GPS sample
 * (no fix until there is one). Lightweight: no NMEA, no other output
 * messages, no satellites; the baud rate is whatever the UART uses.
 */
#ifndef ATHRILL_STM32_UBLOX_H
#define ATHRILL_STM32_UBLOX_H

#include "std_types.h"
#include "cpu.h"
#include "std_device_ops.h"
#include "peripheral/stm32_uart.h"

#define UBLOX_FRAME_MAX	512U

typedef struct {
	uint32 uart;		/* the UART number it is on */
	uint64 cpu_hz;
	/* the frame being received from the host */
	uint8 frame[UBLOX_FRAME_MAX];
	uint32 frame_len;
	/* configuration */
	uint32 pvt_rate;	/* NAV-PVT every pvt_rate measurements (0: off) */
	uint32 dop_rate;
	uint32 meas_rate_ms;	/* the measurement period */
	uint64 measurements;	/* sent since reset */
	uint64 next_clock;	/* the clock of the next measurement */
} UbloxType;

extern const Stm32UartChipOps ublox_ops;

void ublox_reset(UbloxType *gps, uint32 uart, uint64 cpu_hz);
/* Measurements up to the device clock; the clocks to the next one. */
void ublox_tick(UbloxType *gps, DeviceClockType *clock);

#endif

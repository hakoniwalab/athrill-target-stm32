/*
 * The u-blox M8 receiver (see ublox.h). Message layouts from the u-blox 8 /
 * u-blox M8 receiver description (UBX-13003221).
 */
#include "ublox.h"
#include "sensor_source.h"
#include <string.h>

#define SYNC1		0xB5U
#define SYNC2		0x62U
#define CLASS_NAV	0x01U
#define CLASS_ACK	0x05U
#define CLASS_CFG	0x06U
#define CLASS_MON	0x0AU
#define ID_NAV_DOP	0x04U
#define ID_NAV_PVT	0x07U
#define ID_ACK_NAK	0x00U
#define ID_ACK_ACK	0x01U
#define ID_CFG_PRT	0x00U
#define ID_CFG_MSG	0x01U
#define ID_CFG_RATE	0x08U
#define ID_CFG_VALSET	0x8AU
#define ID_MON_VER	0x04U
#define PORT_UART1	1U	/* the rate of CFG-MSG's 8-byte form for UART1 */

#define CFG_PRT_LEN	20U
#define CFG_MSG_LEN	8U
#define NAV_PVT_LEN	92U
#define NAV_DOP_LEN	18U
#define MON_VER_LEN	(30U + 10U + 30U)

#define PORT_ID		1U
#define PORT_MODE	0x000008D0U
#define PORT_BAUD	230400U
#define PORT_PROTO_UBX	0x0001U

/* The receiver's clock starts on 2026-11-04 00:00:00 UTC, a Wednesday (day 3 of the GPS week). */
#define START_YEAR	2026U
#define START_MONTH	11U
#define START_DAY	4U
#define START_WEEKDAY	3U
#define LEAP_SECONDS	18U

static void put16(uint8 *p, uint32 v)
{
	p[0] = (uint8)v;
	p[1] = (uint8)(v >> 8);
}

static void put32(uint8 *p, uint32 v)
{
	put16(p, v);
	put16(p + 2, v >> 16);
}

/* A UBX frame to the host: sync, class, id, length, payload, Fletcher checksum over class to payload. */
static void send(const UbloxType *gps, uint8 cls, uint8 id, const uint8 *payload, uint32 len)
{
	uint8 head[6];
	uint8 ck_a = 0U;
	uint8 ck_b = 0U;
	uint32 i;

	head[0] = SYNC1;
	head[1] = SYNC2;
	head[2] = cls;
	head[3] = id;
	put16(&head[4], len);
	for (i = 0U; i < 6U; i++) {
		(void)stm32_uart_chip_send(gps->uart, head[i]);
		if (i >= 2U) {
			ck_a = (uint8)(ck_a + head[i]);
			ck_b = (uint8)(ck_b + ck_a);
		}
	}
	for (i = 0U; i < len; i++) {
		(void)stm32_uart_chip_send(gps->uart, payload[i]);
		ck_a = (uint8)(ck_a + payload[i]);
		ck_b = (uint8)(ck_b + ck_a);
	}
	(void)stm32_uart_chip_send(gps->uart, ck_a);
	(void)stm32_uart_chip_send(gps->uart, ck_b);
}

static void acknowledge(const UbloxType *gps, std_bool ack, uint8 cls, uint8 id)
{
	uint8 payload[2];

	payload[0] = cls;
	payload[1] = id;
	send(gps, CLASS_ACK, ack ? ID_ACK_ACK : ID_ACK_NAK, payload, 2U);
}

static void mon_ver(const UbloxType *gps)
{
	uint8 payload[MON_VER_LEN];

	memset(payload, 0, sizeof(payload));
	memcpy(&payload[0], "ROM CORE 3.01 (107888)", 22U);	/* swVersion[30] */
	memcpy(&payload[30], "00080000", 8U);			/* hwVersion[10] */
	memcpy(&payload[40], "PROTVER=18.00", 13U);		/* extension[30] */
	send(gps, CLASS_MON, ID_MON_VER, payload, MON_VER_LEN);
}

/* UBX-13003221 32.10.25.2: the UART configuration returned for the active port. */
static void cfg_prt(const UbloxType *gps)
{
	uint8 payload[CFG_PRT_LEN];

	memset(payload, 0, sizeof(payload));
	payload[0] = PORT_ID;
	put16(&payload[2], 0U);
	put32(&payload[4], PORT_MODE);
	put32(&payload[8], PORT_BAUD);
	put16(&payload[12], PORT_PROTO_UBX);
	put16(&payload[14], PORT_PROTO_UBX);
	put16(&payload[16], 0U);
	send(gps, CLASS_CFG, ID_CFG_PRT, payload, CFG_PRT_LEN);
}

static uint32 message_rate(const UbloxType *gps, uint8 cls, uint8 id)
{
	if ((cls == CLASS_NAV) && (id == ID_NAV_PVT)) {
		return gps->pvt_rate;
	}
	if ((cls == CLASS_NAV) && (id == ID_NAV_DOP)) {
		return gps->dop_rate;
	}
	return 0U;
}

/* UBX-13003221 32.10.18.1-.2: poll response with one rate for each port. */
static void cfg_msg(const UbloxType *gps, uint8 cls, uint8 id)
{
	uint8 payload[CFG_MSG_LEN];

	memset(payload, 0, sizeof(payload));
	payload[0] = cls;
	payload[1] = id;
	payload[2U + PORT_UART1] = (uint8)message_rate(gps, cls, id);
	send(gps, CLASS_CFG, ID_CFG_MSG, payload, CFG_MSG_LEN);
}

/* A complete frame from the host (checked). */
static void handle(UbloxType *gps, uint8 cls, uint8 id, const uint8 *payload, uint32 len)
{
	uint32 rate;

	if (cls == CLASS_CFG) {
		/* The zero-length form selects the active UART; the M8 reply is 32.10.25.2. */
		if ((id == ID_CFG_PRT) && (len == 0U)) {
			cfg_prt(gps);
			return;
		}
		if ((id == ID_CFG_MSG) && (len == 2U)) {
			cfg_msg(gps, payload[0], payload[1]);
			return;
		}
		if (id == ID_CFG_VALSET) {
			acknowledge(gps, FALSE, cls, id);
			return;
		}
		if ((id == ID_CFG_MSG) && (len >= 3U)) {
			rate = (len >= 8U) ? payload[2U + PORT_UART1] : payload[2];
			if ((payload[0] == CLASS_NAV) && (payload[1] == ID_NAV_PVT)) {
				gps->pvt_rate = rate;
			}
			else if ((payload[0] == CLASS_NAV) && (payload[1] == ID_NAV_DOP)) {
				gps->dop_rate = rate;
			}
		}
		if ((id == ID_CFG_RATE) && (len >= 2U)) {
			gps->meas_rate_ms = (uint32)payload[0] | ((uint32)payload[1] << 8);
			if (gps->meas_rate_ms == 0U) {
				gps->meas_rate_ms = 1000U;
			}
		}
		acknowledge(gps, TRUE, cls, id);
		return;
	}
	if ((cls == CLASS_MON) && (id == ID_MON_VER) && (len == 0U)) {
		mon_ver(gps);
	}
}

/* A byte from the host: frames are collected from their sync bytes and checked. */
static void chip_transmit(void *p, uint8 data)
{
	UbloxType *gps = (UbloxType *)p;
	uint32 len;
	uint8 ck_a = 0U;
	uint8 ck_b = 0U;
	uint32 i;

	if (((gps->frame_len == 0U) && (data != SYNC1)) || ((gps->frame_len == 1U) && (data != SYNC2))) {
		gps->frame_len = 0U;
		return;
	}
	gps->frame[gps->frame_len++] = data;
	if (gps->frame_len < 6U) {
		return;
	}
	len = (uint32)gps->frame[4] | ((uint32)gps->frame[5] << 8);
	if ((len + 8U) > UBLOX_FRAME_MAX) {
		gps->frame_len = 0U;
		return;
	}
	if (gps->frame_len < (len + 8U)) {
		return;
	}
	for (i = 2U; i < (len + 6U); i++) {
		ck_a = (uint8)(ck_a + gps->frame[i]);
		ck_b = (uint8)(ck_b + ck_a);
	}
	if ((ck_a == gps->frame[len + 6U]) && (ck_b == gps->frame[len + 7U])) {
		handle(gps, gps->frame[2], gps->frame[3], &gps->frame[6], len);
	}
	gps->frame_len = 0U;
}

const Stm32UartChipOps ublox_ops = { chip_transmit };

/* The UTC time of a measurement: the receiver's clock is the virtual clock. */
static void time_of(const UbloxType *gps, uint64 clock, uint8 *pvt, uint32 *itow)
{
	uint64 ms = (clock * 1000U) / gps->cpu_hz;
	uint64 day_ms = ms % 86400000U;
	uint32 day = (uint32)(ms / 86400000U);

	*itow = (uint32)(((((uint64)START_WEEKDAY + day) % 7U) * 86400000U + day_ms + (LEAP_SECONDS * 1000U))
		% (7U * 86400000U));
	put16(&pvt[4], START_YEAR);
	pvt[6] = (uint8)START_MONTH;
	pvt[7] = (uint8)(START_DAY + day);	/* a run stays well within the month */
	pvt[8] = (uint8)(day_ms / 3600000U);
	pvt[9] = (uint8)((day_ms / 60000U) % 60U);
	pvt[10] = (uint8)((day_ms / 1000U) % 60U);
	pvt[11] = 0x07U;	/* validDate, validTime, fullyResolved */
	put32(&pvt[16], (uint32)((day_ms % 1000U) * 1000000U));
}

static void measure(UbloxType *gps, uint64 clock)
{
	uint8 pvt[NAV_PVT_LEN];
	uint8 dop[NAV_DOP_LEN];
	GpsSampleType g;
	uint32 itow;
	std_bool fix;

	memset(pvt, 0, sizeof(pvt));
	time_of(gps, clock, pvt, &itow);
	put32(&pvt[0], itow);
	put32(&pvt[12], 50U);	/* tAcc ns */
	fix = sensor_source_gps(&g) && (g.fix_type >= 2U);
	if (fix) {
		pvt[20] = (uint8)g.fix_type;
		pvt[21] = 0x01U;	/* gnssFixOK */
		pvt[23] = (uint8)g.satellites;
		put32(&pvt[24], (uint32)g.lon);
		put32(&pvt[28], (uint32)g.lat);
		put32(&pvt[32], (uint32)g.alt);	/* no geoid: ellipsoid height = MSL */
		put32(&pvt[36], (uint32)g.alt);
		put32(&pvt[40], g.eph * 10U);
		put32(&pvt[44], g.epv * 10U);
		put32(&pvt[48], (uint32)(g.vn * 10));
		put32(&pvt[52], (uint32)(g.ve * 10));
		put32(&pvt[56], (uint32)(g.vd * 10));
		put32(&pvt[60], g.vel * 10U);
		put32(&pvt[64], g.cog * 1000U);
		put32(&pvt[68], 300U);		/* sAcc mm/s */
		put32(&pvt[72], 500000U);	/* headAcc 5 deg */
		put16(&pvt[76], 120U);		/* pDOP 1.2 */
	}
	else {
		put32(&pvt[40], 999999U);
		put32(&pvt[44], 999999U);
		put16(&pvt[76], 9999U);
	}
	if ((gps->pvt_rate != 0U) && ((gps->measurements % gps->pvt_rate) == 0U)) {
		send(gps, CLASS_NAV, ID_NAV_PVT, pvt, NAV_PVT_LEN);
	}
	if ((gps->dop_rate != 0U) && ((gps->measurements % gps->dop_rate) == 0U)) {
		memset(dop, 0, sizeof(dop));
		put32(&dop[0], itow);
		put16(&dop[4], 150U);	/* gDOP */
		put16(&dop[6], 120U);	/* pDOP */
		put16(&dop[8], 100U);	/* tDOP */
		put16(&dop[10], 100U);	/* vDOP */
		put16(&dop[12], 90U);	/* hDOP */
		put16(&dop[14], 70U);	/* nDOP */
		put16(&dop[16], 70U);	/* eDOP */
		send(gps, CLASS_NAV, ID_NAV_DOP, dop, NAV_DOP_LEN);
	}
	gps->measurements++;
}

void ublox_reset(UbloxType *gps, uint32 uart, uint64 cpu_hz)
{
	memset(gps, 0, sizeof(*gps));
	gps->uart = uart;
	gps->cpu_hz = cpu_hz;
	gps->meas_rate_ms = 1000U;
}

void ublox_tick(UbloxType *gps, DeviceClockType *clock)
{
	uint64 period;

	if ((gps->pvt_rate == 0U) && (gps->dop_rate == 0U)) {
		gps->next_clock = 0U;
		return;
	}
	period = (gps->cpu_hz / 1000U) * gps->meas_rate_ms;
	if (gps->next_clock == 0U) {
		gps->next_clock = clock->clock + period;
	}
	if (clock->clock >= gps->next_clock) {
		measure(gps, clock->clock);
		gps->next_clock += period * (1U + ((clock->clock - gps->next_clock) / period));
	}
	if ((gps->next_clock - clock->clock) < clock->min_intr_interval) {
		clock->min_intr_interval = gps->next_clock - clock->clock;
	}
}

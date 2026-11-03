/*
 * Unit tests of the u-blox M8 model (src/device/sensor/ublox.c): the UBX
 * exchange PX4's gps driver has with it. The UART and the sensor source
 * are stubbed: what the receiver sends is collected and parsed back.
 */
#include <stdio.h>
#include <string.h>
#include "std_errno.h"
#include "sensor/ublox.h"
#include "sensor/sensor_source.h"

#define CPU_HZ	216000000U

static uint8 out[4096];
static uint32 out_len;
static std_bool has_gps;

Std_ReturnType stm32_uart_chip_send(uint32 number, uint8 data)
{
	(void)number;
	if (out_len < sizeof(out)) {
		out[out_len++] = data;
	}
	return STD_E_OK;
}

std_bool sensor_source_gps(GpsSampleType *g)
{
	if (!has_gps) {
		return FALSE;
	}
	memset(g, 0, sizeof(*g));
	g->lat = 356812360;
	g->lon = 1397671250;
	g->alt = 40000;
	g->eph = 100U;
	g->epv = 150U;
	g->vn = 120;
	g->ve = -50;
	g->vd = 10;
	g->vel = 130U;
	g->cog = 33700U;
	g->fix_type = 3U;
	g->satellites = 12U;
	g->time_usec = 1U;
	return TRUE;
}

static int failures;
static int checks;

#define CHECK_EQ(what, got, want) check_eq(__LINE__, what, (long long)(got), (long long)(want))

static void check_eq(int line, const char *what, long long got, long long want)
{
	checks++;
	if (got != want) {
		failures++;
		printf("FAIL line %d: %s = %lld (0x%llx), want %lld (0x%llx)\n", line, what, got, got, want, want);
	}
}

static UbloxType gps;

/* A UBX frame from the host. */
static void host_send(uint8 cls, uint8 id, const uint8 *payload, uint32 len)
{
	uint8 f[64];
	uint8 a = 0U;
	uint8 b = 0U;
	uint32 i;

	f[0] = 0xB5U;
	f[1] = 0x62U;
	f[2] = cls;
	f[3] = id;
	f[4] = (uint8)len;
	f[5] = (uint8)(len >> 8);
	memcpy(&f[6], payload, len);
	for (i = 2U; i < (len + 6U); i++) {
		a = (uint8)(a + f[i]);
		b = (uint8)(b + a);
	}
	f[len + 6U] = a;
	f[len + 7U] = b;
	for (i = 0U; i < (len + 8U); i++) {
		ublox_ops.transmit(&gps, f[i]);
	}
}

/* The n-th frame the receiver sent (checksum checked): its class, id, payload. */
static std_bool frame(uint32 n, uint8 *cls, uint8 *id, const uint8 **payload, uint32 *len)
{
	uint32 pos = 0U;

	while ((pos + 8U) <= out_len) {
		uint32 l = (uint32)out[pos + 4U] | ((uint32)out[pos + 5U] << 8);
		uint8 a = 0U;
		uint8 b = 0U;
		uint32 i;

		if ((out[pos] != 0xB5U) || (out[pos + 1U] != 0x62U)) {
			return FALSE;
		}
		for (i = pos + 2U; i < (pos + l + 6U); i++) {
			a = (uint8)(a + out[i]);
			b = (uint8)(b + a);
		}
		if ((a != out[pos + l + 6U]) || (b != out[pos + l + 7U])) {
			return FALSE;
		}
		if (n == 0U) {
			*cls = out[pos + 2U];
			*id = out[pos + 3U];
			*payload = &out[pos + 6U];
			*len = l;
			return TRUE;
		}
		n--;
		pos += l + 8U;
	}
	return FALSE;
}

static uint32 u32(const uint8 *p)
{
	return (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24);
}

static void test_configuration(void)
{
	uint8 cls;
	uint8 id;
	const uint8 *p;
	uint32 len;
	uint8 valset[8] = { 0 };
	uint8 prt[20] = { 1 };
	uint8 bad[4] = { 1, 2, 3, 4 };
	uint32 i;

	ublox_reset(&gps, 1U, CPU_HZ);
	out_len = 0U;
	host_send(0x06U, 0x8AU, valset, sizeof(valset));
	CHECK_EQ("CFG-VALSET: a frame back", frame(0U, &cls, &id, &p, &len), TRUE);
	CHECK_EQ("CFG-VALSET: ACK-NAK", (cls << 8) | id, 0x0500);
	CHECK_EQ("NAK of CFG-VALSET", (p[0] << 8) | p[1], 0x068A);
	out_len = 0U;
	host_send(0x06U, 0x00U, prt, sizeof(prt));
	CHECK_EQ("CFG-PRT: ACK-ACK", frame(0U, &cls, &id, &p, &len) && (cls == 0x05U) && (id == 0x01U), TRUE);
	CHECK_EQ("ACK of CFG-PRT", (p[0] << 8) | p[1], 0x0600);
	out_len = 0U;
	host_send(0x0AU, 0x04U, NULL, 0U);
	CHECK_EQ("MON-VER", frame(0U, &cls, &id, &p, &len) && (cls == 0x0AU) && (id == 0x04U), TRUE);
	CHECK_EQ("MON-VER hwVersion M8", memcmp(&p[30], "00080000", 9U), 0);
	CHECK_EQ("MON-VER PROTVER", memcmp(&p[40], "PROTVER=18.00", 13U), 0);
	/* a frame with a bad checksum is ignored */
	out_len = 0U;
	for (i = 0U; i < 4U; i++) {
		ublox_ops.transmit(&gps, (uint8[]){ 0xB5U, 0x62U, 0x06U, 0x00U }[i]);
	}
	ublox_ops.transmit(&gps, 0U);
	ublox_ops.transmit(&gps, 0U);
	ublox_ops.transmit(&gps, 0xFFU);
	ublox_ops.transmit(&gps, 0xFFU);
	CHECK_EQ("bad checksum: no answer", out_len, 0U);
	(void)bad;
}

static void test_measurements(void)
{
	uint8 rate[6] = { 100U, 0U, 1U, 0U, 1U, 0U };	/* 100 ms */
	uint8 msg_pvt[3] = { 0x01U, 0x07U, 1U };
	uint8 msg_dop[8] = { 0x01U, 0x04U, 0U, 2U, 0U, 0U, 0U, 0U };	/* every 2nd on UART1 */
	DeviceClockType clock;
	uint8 cls;
	uint8 id;
	const uint8 *p;
	uint32 len;

	ublox_reset(&gps, 1U, CPU_HZ);
	host_send(0x06U, 0x08U, rate, sizeof(rate));
	host_send(0x06U, 0x01U, msg_pvt, sizeof(msg_pvt));
	host_send(0x06U, 0x01U, msg_dop, sizeof(msg_dop));
	out_len = 0U;
	clock.clock = 1000U;
	clock.min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	ublox_tick(&gps, &clock);
	CHECK_EQ("first measurement in 100 ms", clock.min_intr_interval, CPU_HZ / 10U);
	CHECK_EQ("nothing yet", out_len, 0U);
	/* no GPS sample yet: no fix */
	has_gps = FALSE;
	clock.clock = 1000U + (CPU_HZ / 10U);
	clock.min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	ublox_tick(&gps, &clock);
	CHECK_EQ("NAV-PVT", frame(0U, &cls, &id, &p, &len) && (cls == 0x01U) && (id == 0x07U) && (len == 92U), TRUE);
	CHECK_EQ("no fix", p[20], 0U);
	CHECK_EQ("NAV-DOP with the first measurement", frame(1U, &cls, &id, &p, &len) && (id == 0x04U), TRUE);
	/* the next one, with a fix */
	has_gps = TRUE;
	out_len = 0U;
	clock.clock += CPU_HZ / 10U;
	clock.min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	ublox_tick(&gps, &clock);
	CHECK_EQ("NAV-PVT", frame(0U, &cls, &id, &p, &len) && (id == 0x07U), TRUE);
	CHECK_EQ("3D fix", p[20], 3U);
	CHECK_EQ("gnssFixOK", p[21] & 1U, 1U);
	CHECK_EQ("numSV", p[23], 12U);
	CHECK_EQ("lon", u32(&p[24]), 1397671250U);
	CHECK_EQ("lat", u32(&p[28]), 356812360U);
	CHECK_EQ("hMSL mm", u32(&p[36]), 40000U);
	CHECK_EQ("hAcc mm", u32(&p[40]), 1000U);
	CHECK_EQ("velN mm/s", (sint32)u32(&p[48]), 1200);
	CHECK_EQ("velE mm/s", (sint32)u32(&p[52]), -500);
	CHECK_EQ("headMot 1e-5 deg", u32(&p[64]), 33700000U);
	CHECK_EQ("valid date and time", p[11], 7U);
	CHECK_EQ("year", p[4] | (p[5] << 8), 2026);
	CHECK_EQ("NAV-DOP every 2nd: not now", frame(1U, &cls, &id, &p, &len), FALSE);
}

int main(void)
{
	test_configuration();
	test_measurements();
	printf("%d checks, %d failures\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}

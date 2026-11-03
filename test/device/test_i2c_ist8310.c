/*
 * Unit tests of the I2C model with the IST8310 chip model
 * (src/device/peripheral/stm32_i2c.c, src/device/sensor/ist8310.c), driven
 * as NuttX's interrupt-driven STM32F7 I2C driver does: a register address
 * written, then a repeated START to read. The NVIC and the sensor source
 * are stubbed.
 */
#include <stdio.h>
#include <string.h>
#include "peripheral/stm32_i2c.h"
#include "sensor/ist8310.h"
#include "sensor/sensor_source.h"

static uint32 raised[128];
static uint8 active[128];
std_bool stm32_device_accessed;

int intc_raise_intr(uint32 intno)
{
	raised[intno]++;
	return 0;
}

std_bool intc_irq_active(uint32 intno)
{
	return active[intno] != 0U;
}

void sensor_source_sample(SensorSampleType *s)
{
	memset(s, 0, sizeof(*s));
	s->mag[0] = 0.2F;	/* north, body forward */
	s->mag[1] = -0.05F;
	s->mag[2] = 0.4F;	/* down */
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

#define I2C3	0x40005C00U
#define CR1	0x00U
#define CR2	0x04U
#define ISR	0x18U
#define ICR	0x1CU
#define RXDR	0x24U
#define TXDR	0x28U
#define IRQ	72U

#define TXIS	(1U << 1)
#define RXNE	(1U << 2)
#define NACKF	(1U << 4)
#define STOPF	(1U << 5)
#define TC	(1U << 6)
#define BUSY	(1U << 15)

static Ist8310Type mag;

static void cr2(uint32 address, std_bool read, uint32 n)
{
	stm32_i2c_write(I2C3 + CR2, 4U, (address << 1) | (read ? (1U << 10) : 0U) | (n << 16) | (1U << 13));
}

static void setup(void)
{
	memset(raised, 0, sizeof(raised));
	memset(active, 0, sizeof(active));
	stm32_i2c_reset();
	ist8310_reset(&mag, IST8310_ROTATION_ROLL_180_YAW_90);
	stm32_i2c_attach(3U, 0x0EU, &ist8310_ops, &mag);
	stm32_i2c_write(I2C3 + CR1, 4U, 0xD7U);	/* PE, TXIE, RXIE, NACKIE, STOPIE, TCIE, ERRIE */
}

/* Writes reg (and a value if n == 2), then reads count bytes after a repeated START. */
static void transfer(uint8 r, const uint8 *wdata, uint32 wn, uint8 *rdata, uint32 rn)
{
	uint32 i;

	cr2(0x0EU, FALSE, 1U + wn);
	stm32_i2c_write(I2C3 + TXDR, 1U, r);
	for (i = 0U; i < wn; i++) {
		stm32_i2c_write(I2C3 + TXDR, 1U, wdata[i]);
	}
	if (rn == 0U) {
		stm32_i2c_write(I2C3 + CR2, 4U, stm32_i2c_read(I2C3 + CR2) | (1U << 14));
		return;
	}
	cr2(0x0EU, TRUE, rn);
	for (i = 0U; i < rn; i++) {
		rdata[i] = (uint8)stm32_i2c_read(I2C3 + RXDR);
	}
	stm32_i2c_write(I2C3 + CR2, 4U, stm32_i2c_read(I2C3 + CR2) | (1U << 14));
}

static sint16 le16(const uint8 *p)
{
	return (sint16)(((uint16)p[1] << 8) | p[0]);
}

static void test_flags(void)
{
	setup();
	cr2(0x0EU, FALSE, 1U);
	CHECK_EQ("address ACK: TXIS, BUSY", stm32_i2c_read(I2C3 + ISR) & (TXIS | BUSY), TXIS | BUSY);
	CHECK_EQ("TXIS interrupts (I2C3_EV)", raised[IRQ], 1U);
	stm32_i2c_write(I2C3 + TXDR, 1U, 0x00U);
	CHECK_EQ("NBYTES sent, no AUTOEND: TC", stm32_i2c_read(I2C3 + ISR) & (TC | TXIS), TC);
	cr2(0x0EU, TRUE, 1U);
	CHECK_EQ("repeated START read: RXNE and TC at once", stm32_i2c_read(I2C3 + ISR) & (RXNE | TC), RXNE | TC);
	CHECK_EQ("WAI", stm32_i2c_read(I2C3 + RXDR), 0x10U);
	/* TC stays: the level interrupt is pending again once the handler ends */
	raised[IRQ] = 0U;
	active[IRQ] = 1U;
	stm32_i2c_tick();
	CHECK_EQ("TC high, handler active: not again", raised[IRQ], 0U);
	active[IRQ] = 0U;
	stm32_i2c_tick();
	CHECK_EQ("TC high, handler done: pending again", raised[IRQ], 1U);
	stm32_i2c_write(I2C3 + CR2, 4U, stm32_i2c_read(I2C3 + CR2) | (1U << 14));
	CHECK_EQ("STOP: STOPF, not busy", stm32_i2c_read(I2C3 + ISR) & (STOPF | BUSY | TC), STOPF);
	stm32_i2c_write(I2C3 + ICR, 4U, 1U << 5);
	CHECK_EQ("ICR STOPCF", stm32_i2c_read(I2C3 + ISR) & STOPF, 0U);
	/* no chip at the address: NACKF and an automatic STOP */
	cr2(0x1EU, FALSE, 1U);
	CHECK_EQ("NACK", stm32_i2c_read(I2C3 + ISR) & (NACKF | STOPF | BUSY | TXIS), NACKF | STOPF);
	/* AUTOEND: a STOP after NBYTES */
	stm32_i2c_write(I2C3 + ICR, 4U, 0x3F38U);
	stm32_i2c_write(I2C3 + CR2, 4U, (0x0EU << 1) | (1U << 10) | (2U << 16) | (1U << 25) | (1U << 13));
	(void)stm32_i2c_read(I2C3 + RXDR);
	(void)stm32_i2c_read(I2C3 + RXDR);
	CHECK_EQ("AUTOEND: STOPF after the last byte", stm32_i2c_read(I2C3 + ISR) & (STOPF | BUSY), STOPF);
	/* PE = 0 resets the flags */
	stm32_i2c_write(I2C3 + CR1, 4U, 0U);
	CHECK_EQ("PE 0: flags cleared, TXE", stm32_i2c_read(I2C3 + ISR), 1U);
}

static void test_ist8310(void)
{
	uint8 buf[8];
	uint8 v;

	setup();
	v = 0x01U;	/* SRST */
	transfer(0x0BU, &v, 1U, NULL, 0U);
	transfer(0x0BU, NULL, 0U, buf, 1U);
	CHECK_EQ("CNTL2: SRST done", buf[0], 0U);
	v = 0x70U;
	transfer(0x0DU, &v, 1U, NULL, 0U);
	transfer(0x0DU, NULL, 0U, buf, 1U);
	CHECK_EQ("CNTL3 read back", buf[0], 0x70U);
	transfer(0x02U, NULL, 0U, buf, 7U);
	CHECK_EQ("no measurement: no DRDY", buf[0] & 1U, 0U);
	v = 0x01U;	/* single measurement */
	transfer(0x0AU, &v, 1U, NULL, 0U);
	transfer(0x02U, NULL, 0U, buf, 7U);
	CHECK_EQ("DRDY", buf[0] & 1U, 1U);
	/* roll 180 yaw 90: raw x = body y, raw y = body x, raw z = body z (PX4 flips z) */
	CHECK_EQ("x = body right", le16(&buf[1]), (sint16)(-0.05F * 1320.0F - 0.5F));
	CHECK_EQ("y = body forward", le16(&buf[3]), (sint16)(0.2F * 1320.0F + 0.5F));
	CHECK_EQ("z = body down", le16(&buf[5]), (sint16)(0.4F * 1320.0F + 0.5F));
	transfer(0x02U, NULL, 0U, buf, 1U);
	CHECK_EQ("data read: DRDY cleared", buf[0] & 1U, 0U);
	transfer(0x0AU, NULL, 0U, buf, 1U);
	CHECK_EQ("CNTL1 back to stand-by", buf[0], 0U);
}

int main(void)
{
	test_flags();
	test_ist8310();
	printf("%d checks, %d failures\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}

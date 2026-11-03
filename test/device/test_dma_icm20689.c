/*
 * Unit tests of the DMA and EXTI models with the ICM-20689 chip model
 * (src/device/peripheral/stm32_dma.c, stm32_exti.c, src/device/sensor/icm20689.c),
 * driven as NuttX's SPI DMA and PX4's icm20689 driver do. The CPU clock, the
 * NVIC, the bus (memory) and the sensor source are stubbed.
 */
#include <stdio.h>
#include <string.h>
#include "peripheral/stm32_gpio.h"
#include "peripheral/stm32_spi.h"
#include "peripheral/stm32_exti.h"
#include "peripheral/stm32_dma.h"
#include "sensor/icm20689.h"
#include "sensor/sensor_source.h"
#include "bus.h"

/* stubs */
static uint64 now;
static uint32 raised[128];
static uint8 ram[0x1000];
#define RAM_BASE	0x20000000U

uint64 cpuemu_get_clock(void)
{
	return now;
}

int intc_raise_intr(uint32 intno)
{
	raised[intno]++;
	return 0;
}

void sensor_source_sample(SensorSampleType *s)
{
	memset(s, 0, sizeof(*s));
	s->acc[0] = 1.0F;	/* forward */
	s->acc[1] = 2.0F;	/* right */
	s->acc[2] = -9.80665F;	/* down: at rest the specific force points up */
	s->gyro[0] = 0.1F;
	s->gyro[1] = -0.2F;
	s->gyro[2] = 0.3F;
	s->temperature = 25.0F;
}

Std_ReturnType mpu_get_data8(CoreIdType c, AthrillAddrType a, uint8 *d)
{
	(void)c;
	*d = ram[a - RAM_BASE];
	return STD_E_OK;
}

Std_ReturnType mpu_get_data16(CoreIdType c, AthrillAddrType a, uint16 *d)
{
	(void)c;
	memcpy(d, &ram[a - RAM_BASE], 2U);
	return STD_E_OK;
}

Std_ReturnType mpu_get_data32(CoreIdType c, AthrillAddrType a, uint32 *d)
{
	(void)c;
	memcpy(d, &ram[a - RAM_BASE], 4U);
	return STD_E_OK;
}

Std_ReturnType mpu_put_data8(CoreIdType c, AthrillAddrType a, uint8 d)
{
	(void)c;
	ram[a - RAM_BASE] = d;
	return STD_E_OK;
}

Std_ReturnType mpu_put_data16(CoreIdType c, AthrillAddrType a, uint16 d)
{
	(void)c;
	memcpy(&ram[a - RAM_BASE], &d, 2U);
	return STD_E_OK;
}

Std_ReturnType mpu_put_data32(CoreIdType c, AthrillAddrType a, uint32 d)
{
	(void)c;
	memcpy(&ram[a - RAM_BASE], &d, 4U);
	return STD_E_OK;
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

#define GPIOF	0x40021400U
#define SPI1	0x40013000U
#define DMA2	0x40026400U
#define EXTI	0x40013C00U
#define EXTICR2	0x4001380CU
#define CPU_HZ	216000000U
#define SAMPLE	(CPU_HZ / 8000U)

static Icm20689Type imu;

static void cs(std_bool low)
{
	stm32_gpio_write(GPIOF + 0x18U, 4U, low ? (1U << (2U + 16U)) : (1U << 2U));
}

static uint8 send(uint8 b)
{
	stm32_spi_write(SPI1 + 0x0CU, 1U, b);
	return (uint8)stm32_spi_read(SPI1 + 0x0CU, 1U);
}

static uint8 reg_read(uint8 r)
{
	uint8 v;

	cs(TRUE);
	(void)send((uint8)(r | 0x80U));
	v = send(0U);
	cs(FALSE);
	return v;
}

static void reg_write(uint8 r, uint8 v)
{
	cs(TRUE);
	(void)send(r);
	(void)send(v);
	cs(FALSE);
}

/* NuttX: RX stream 0 then TX stream 3 of DMA2, 8-bit, memory increment; SPI CR2 RXDMAEN/TXDMAEN set. */
static void dma_exchange(uint32 tx, uint32 rx, uint32 n)
{
	uint32 rx_cr = (1U << 10) | (1U << 4) | (3U << 25);			/* MINC, TCIE, P2M */
	uint32 tx_cr = (1U << 10) | (1U << 6) | (3U << 25);			/* MINC, M2P */

	stm32_dma_write(DMA2 + 0x10U + 0x04U, 4U, n);
	stm32_dma_write(DMA2 + 0x10U + 0x08U, 4U, SPI1 + 0x0CU);
	stm32_dma_write(DMA2 + 0x10U + 0x0CU, 4U, rx);
	stm32_dma_write(DMA2 + 0x10U, 4U, rx_cr);
	stm32_dma_write(DMA2 + 0x10U, 4U, rx_cr | 1U);
	stm32_dma_write(DMA2 + 0x58U + 0x04U, 4U, n);
	stm32_dma_write(DMA2 + 0x58U + 0x08U, 4U, SPI1 + 0x0CU);
	stm32_dma_write(DMA2 + 0x58U + 0x0CU, 4U, tx);
	stm32_dma_write(DMA2 + 0x58U, 4U, tx_cr);
	stm32_dma_write(DMA2 + 0x58U, 4U, tx_cr | 1U);
}

static sint16 be16(const uint8 *p)
{
	return (sint16)(((uint16)p[0] << 8) | p[1]);
}

static void setup(void)
{
	memset(ram, 0, sizeof(ram));
	memset(raised, 0, sizeof(raised));
	now = 0U;
	stm32_gpio_reset();
	stm32_exti_reset();
	stm32_dma_reset();
	stm32_spi_reset();
	icm20689_reset(&imu, CPU_HZ, ICM20689_ROTATION_YAW_90, STM32_GPIO_PORT('B'), 4U);
	stm32_spi_attach(1U, STM32_GPIO_PORT('F'), 2U, &icm20689_ops, &imu);
	stm32_gpio_write(GPIOF, 4U, 1U << 4);	/* PF2 output */
	cs(FALSE);
	stm32_spi_write(SPI1 + 0x04U, 4U, (7U << 8) | (1U << 12) | 3U);	/* 8-bit, FRXTH, RX/TX DMA */
	stm32_spi_write(SPI1, 4U, (1U << 6) | (1U << 2));
}

static void configure(void)
{
	reg_write(0x6BU, 0x01U);	/* PWR_MGMT_1: CLKSEL 1, awake */
	reg_write(0x1AU, 0x47U);	/* CONFIG: FIFO_MODE, DLPF bypass 8 kHz */
	reg_write(0x1BU, 0x18U);	/* GYRO_CONFIG: 2000 dps */
	reg_write(0x1CU, 0x18U);	/* ACCEL_CONFIG: 16 g */
	reg_write(0x23U, 0x78U);	/* FIFO_EN: XG YG ZG ACCEL */
	reg_write(0x37U, 0x80U);	/* INT_PIN_CFG: active low */
	reg_write(0x38U, 0x01U);	/* INT_ENABLE: DATA_RDY */
	reg_write(0x6AU, 0x54U);	/* USER_CTRL: FIFO_EN, I2C_IF_DIS, FIFO_RST */
}

static void test_registers(void)
{
	setup();
	CHECK_EQ("WHO_AM_I", reg_read(0x75U), 0x98U);
	CHECK_EQ("PWR_MGMT_1 after reset: SLEEP", reg_read(0x6BU), 0x40U);
	configure();
	CHECK_EQ("GYRO_CONFIG read back", reg_read(0x1BU), 0x18U);
	CHECK_EQ("USER_CTRL: FIFO_RST self-clears", reg_read(0x6AU), 0x50U);
	reg_write(0x6BU, 0x80U);
	CHECK_EQ("DEVICE_RESET: registers back to reset", reg_read(0x1BU), 0U);
	CHECK_EQ("DEVICE_RESET: SLEEP again", reg_read(0x6BU), 0x40U);
}

static void test_fifo_dma(void)
{
	uint8 count[3];
	uint32 n;
	const uint8 *f;
	float32 g16 = 2048.0F / 9.80665F;

	setup();
	configure();
	now = 20U * SAMPLE + 5U;
	/* FIFO count by polled SPI (3 bytes, below NuttX's DMA threshold) */
	cs(TRUE);
	(void)send(0x72U | 0x80U);
	count[0] = send(0U);
	count[1] = send(0U);
	cs(FALSE);
	n = ((uint32)count[0] << 8) | count[1];
	CHECK_EQ("FIFO count: 20 records of 12 bytes", n, 240U);
	/* the burst read by DMA: the command and 20 records */
	ram[0] = 0x74U | 0x80U;
	cs(TRUE);
	dma_exchange(RAM_BASE, RAM_BASE + 0x400U, 1U + n);
	cs(FALSE);
	CHECK_EQ("DMA2 stream 0 (RX) transfer complete", (stm32_dma_read(DMA2) >> 5) & 1U, 1U);
	CHECK_EQ("DMA2 stream 3 (TX) transfer complete", (stm32_dma_read(DMA2) >> 27) & 1U, 1U);
	CHECK_EQ("RX stream disabled, NDTR 0", stm32_dma_read(DMA2 + 0x10U) & 1U, 0U);
	CHECK_EQ("RX interrupt (DMA2 stream 0, IRQ 56)", raised[56], 1U);
	f = &ram[0x401];
	/* yaw 90 mounting: chip x = body y (right), chip y = body x (forward), chip z = -body z */
	CHECK_EQ("accel x = body right", be16(&f[0]), (sint16)(2.0F * g16 + 0.5F));
	CHECK_EQ("accel y = body forward", be16(&f[2]), (sint16)(1.0F * g16 + 0.5F));
	CHECK_EQ("accel z = 1 g up", be16(&f[4]), (sint16)(9.80665F * g16 + 0.5F));
	CHECK_EQ("gyro x = body roll rate about right axis", be16(&f[6]), (sint16)(-0.2F * 16.4F * 57.29577951F - 0.5F));
	CHECK_EQ("gyro y = body forward", be16(&f[8]), (sint16)(0.1F * 16.4F * 57.29577951F + 0.5F));
	CHECK_EQ("gyro z = -body down", be16(&f[10]), (sint16)(-0.3F * 16.4F * 57.29577951F - 0.5F));
	CHECK_EQ("second record repeats (accel at half rate)", memcmp(&f[0], &f[12], 6), 0);
	stm32_dma_write(DMA2 + 0x08U, 4U, 0x0F7D0F7DU);
	CHECK_EQ("LIFCR clears the flags", stm32_dma_read(DMA2), 0U);
	/* read out: the FIFO is empty */
	cs(TRUE);
	(void)send(0x72U | 0x80U);
	n = (uint32)send(0U) << 8;
	n |= send(0U);
	cs(FALSE);
	CHECK_EQ("FIFO empty after the burst", n, 0U);
	/* overflow: capped at 512 bytes */
	now += 100U * SAMPLE;
	cs(TRUE);
	(void)send(0x72U | 0x80U);
	n = (uint32)send(0U) << 8;
	n |= send(0U);
	cs(FALSE);
	CHECK_EQ("FIFO count capped at 512", n, 512U);
}

static void test_data_ready(void)
{
	DeviceClockType clock;

	setup();
	/* PB4 on EXTI4, falling edge, unmasked */
	stm32_exti_write(EXTICR2, 4U, 1U);
	stm32_exti_write(EXTI + 0x0CU, 4U, 1U << 4);
	stm32_exti_write(EXTI + 0x00U, 4U, 1U << 4);
	configure();
	clock.clock = 0U;
	clock.min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	icm20689_tick(&imu, &clock);
	CHECK_EQ("next pulse in one sample period", clock.min_intr_interval, SAMPLE);
	CHECK_EQ("no pulse yet", raised[10], 0U);
	clock.clock = SAMPLE;
	clock.min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	icm20689_tick(&imu, &clock);
	CHECK_EQ("pulse: EXTI4 (IRQ 10)", raised[10], 1U);
	CHECK_EQ("EXTI PR line 4", stm32_exti_read(EXTI + 0x14U) & (1U << 4), 1U << 4);
	stm32_exti_write(EXTI + 0x14U, 4U, 1U << 4);
	CHECK_EQ("PR rc_w1", stm32_exti_read(EXTI + 0x14U) & (1U << 4), 0U);
	CHECK_EQ("the pin rests high (active low)", (stm32_gpio_read(0x40020400U + 0x10U) >> 4) & 1U, 1U);
	clock.clock = 3U * SAMPLE + 7U;
	clock.min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	icm20689_tick(&imu, &clock);
	CHECK_EQ("late tick: one pulse", raised[10], 2U);
	CHECK_EQ("next pulse on the sample grid", clock.min_intr_interval, SAMPLE - 7U);
}

int main(void)
{
	test_registers();
	test_fifo_dma();
	test_data_ready();
	printf("%d checks, %d failures\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}

/*
 * The USART transmit DMA path with the real DMA model
 * (src/device/peripheral/stm32_uart.c and stm32_dma.c): a memory-to-TDR
 * stream as ArduPilot's ChibiOS UART driver programs it. Checks the bytes
 * on the line and their times, NDTR, the half-transfer and transfer-complete
 * flags, the stream's interrupt and the stream disabling itself. The CPU
 * clock, the NVIC, the bus (memory), SPI and the console are stubbed.
 *
 * Expectations come from RM0410 Rev 5 sections 8.3.4 (F7 channel
 * selection), 8.3.9 (circular mode), 8.3.14 (completion), 8.4 and 8.5;
 * and RM0433 Rev 8 sections 15.3.5 (DMAMUX request mapping), 15.3.10
 * (circular mode), 15.3.15 (completion), 15.4/15.5, and 17.3.2/17.6.1
 * (DMAMUX channel mapping and DMAREQ_ID).
 */
#include <stdio.h>
#include <string.h>
#include "peripheral/stm32_uart.h"
#include "peripheral/stm32_dma.h"
#include "peripheral/stm32_spi.h"
#include "device.h"
#include "bus.h"
#include "concrete_executor/target/dbg_target_serial.h"

/* stubs of the emulator */
static uint64 now;
static uint32 raised[128];
static uint8 ram[0x100];
static char line_out[64];
static uint32 line_len;
std_bool stm32_device_accessed;
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

std_bool intc_irq_active(uint32 intno)
{
	(void)intno;
	return FALSE;
}

bool dbg_serial_putchar(uint8 c, uint8 d)
{
	(void)c;
	if (line_len < (sizeof(line_out) - 1U)) {
		line_out[line_len++] = (char)d;
		line_out[line_len] = '\0';
	}
	return TRUE;
}

std_bool stm32_spi_owns(uint32 addr)
{
	(void)addr;
	return FALSE;
}

uint32 stm32_spi_read(uint32 addr, uint32 size)
{
	(void)addr;
	(void)size;
	return 0U;
}

void stm32_spi_write(uint32 addr, uint32 size, uint32 value)
{
	(void)addr;
	(void)size;
	(void)value;
}

std_bool stm32h7_spi_dma_exchange(uint32 txdr, uint32 size, uint32 value,
	uint32 *rxdr, uint32 *response)
{
	(void)txdr;
	(void)size;
	(void)value;
	(void)rxdr;
	(void)response;
	return FALSE;
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

#define CHECK_EQ(what, got, want) check_eq(__LINE__, what, (unsigned long long)(got), (unsigned long long)(want))
#define CHECK_STR(what, got, want) check_str(__LINE__, what, got, want)

static void check_eq(int line, const char *what, unsigned long long got, unsigned long long want)
{
	checks++;
	if (got != want) {
		failures++;
		printf("FAIL line %d: %s = 0x%llx (%llu), want 0x%llx (%llu)\n", line, what, got, got, want, want);
	}
}

static void check_str(int line, const char *what, const char *got, const char *want)
{
	checks++;
	if (strcmp(got, want) != 0) {
		failures++;
		printf("FAIL line %d: %s = \"%s\", want \"%s\"\n", line, what, got, want);
	}
}

/* USART2 (APB1, 54 MHz) is the console here, as SERIAL1/TELEM1 in the ArduPilot example */
#define USART2		0x40004400U
#define CR1		0x00U
#define CR3		0x08U
#define BRR		0x0CU
#define ISR		0x1CU
#define RDR		0x24U
#define TDR		0x28U
#define UE		(1U << 0)
#define TE		(1U << 3)
#define DMAT		(1U << 7)
#define TC		(1U << 6)

/* DMA1 stream 6: USART2_TX (RM0410 table 27) */
#define F7_DMA1		0x40026000U
#define H7_DMA1		0x40020000U
#define LISR		0x00U
#define HISR		0x04U
#define HIFCR		0x0CU
#define S6CR		(0x10U + (6U * 0x18U))
#define S6NDTR		(S6CR + 0x04U)
#define S6PAR		(S6CR + 0x08U)
#define S6M0AR		(S6CR + 0x0CU)
#define EN		(1U << 0)
#define HTIE		(1U << 3)
#define TCIE		(1U << 4)
#define DIR_M2P		(1U << 6)
#define MINC		(1U << 10)
#define CHSEL_4		(4U << 25)
#define S6_HTIF		(1U << 20)	/* stream 6 flags start at HISR bit 16 */
#define S6_TCIF		(1U << 21)
#define DMA1_S6_IRQ	17U

#define S5CR		(0x10U + (5U * 0x18U))
#define S5NDTR		(S5CR + 0x04U)
#define S5PAR		(S5CR + 0x08U)
#define S5M0AR		(S5CR + 0x0CU)
#define CIRC		(1U << 8)
#define S5_HTIF		(1U << 10)
#define S5_TCIF		(1U << 11)
#define DMA1_S5_IRQ	16U

#define S0CR		0x10U
#define S0NDTR		0x14U
#define S0PAR		0x18U
#define S0M0AR		0x1CU
#define S0_TCIF		(1U << 5)
#define DMA1_S0_IRQ	11U

#define H7_USART2_RX_REQUEST 43U
#define H7_USART2_TX_REQUEST 44U

/* 54 MHz / 469: 115200 baud; 10 bits of 469 kernel clocks of 4 CPU clocks */
#define FRAME		(10U * 469U * 4U)

static uint32 dma1;

static uint32 uart_isr(void)
{
	return stm32_uart_read(USART2 + ISR);
}

static uint32 dma(uint32 offset)
{
	return stm32_dma_read(dma1 + offset);
}

static uint64 tick(void)
{
	DeviceClockType clock;

	clock.clock = now;
	clock.min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	clock.can_skip_clock = TRUE;
	clock.is_halt = FALSE;
	stm32_uart_tick(&clock);
	return clock.min_intr_interval;
}

static void setup(std_bool tx_dma, std_bool h7)
{
	now = 0U;
	memset(raised, 0, sizeof(raised));
	line_len = 0U;
	line_out[0] = '\0';
	memcpy(ram, "MAVL", 4U);
	stm32_dma_set_h7(h7);
	dma1 = h7 ? H7_DMA1 : F7_DMA1;
	stm32_dma_reset();
	stm32_uart_set_tx_dma(tx_dma);
	stm32_uart_reset(2U, NULL, NULL);
	stm32_uart_write(USART2 + BRR, 4U, 469U);
	stm32_uart_write(USART2 + CR3, 4U, DMAT);
	stm32_uart_write(USART2 + CR1, 4U, UE | TE);
	stm32_dma_write(dma1 + S6PAR, 4U, USART2 + TDR);
	stm32_dma_write(dma1 + S6M0AR, 4U, RAM_BASE);
	stm32_dma_write(dma1 + S6NDTR, 4U, 4U);
	stm32_dma_write(dma1 + S6CR, 4U, CHSEL_4 | MINC | DIR_M2P | TCIE | HTIE | EN);
}

static void test_four_bytes(std_bool h7)
{
	setup(TRUE, h7);
	CHECK_EQ("selected DMA address is owned", stm32_dma_owns(dma1), TRUE);
	CHECK_EQ("other-family DMA address is not owned",
		stm32_dma_owns(h7 ? F7_DMA1 : H7_DMA1), FALSE);
	(void)uart_isr();
	CHECK_STR("first byte straight to the line, second waits in TDR", line_out, "M");
	CHECK_EQ("NDTR after two requests", dma(S6NDTR), 2U);
	CHECK_EQ("half transfer flagged", dma(HISR) & (S6_HTIF | S6_TCIF), S6_HTIF);
	CHECK_EQ("half-transfer interrupt", raised[DMA1_S6_IRQ], 1U);
	CHECK_EQ("the next event: the end of the first frame", tick(), FRAME);

	now = FRAME - 1U;
	(void)uart_isr();
	CHECK_STR("nothing new before the frame ends", line_out, "M");
	CHECK_EQ("NDTR unchanged", dma(S6NDTR), 2U);

	now = FRAME;
	(void)uart_isr();
	CHECK_STR("second byte on the line", line_out, "MA");
	CHECK_EQ("NDTR after the third request", dma(S6NDTR), 1U);

	now = 2U * FRAME;
	(void)uart_isr();
	CHECK_STR("third byte on the line", line_out, "MAV");
	CHECK_EQ("all data moved", dma(S6NDTR), 0U);
	CHECK_EQ("transfer complete flagged", dma(HISR) & (S6_HTIF | S6_TCIF), S6_HTIF | S6_TCIF);
	CHECK_EQ("transfer-complete interrupt", raised[DMA1_S6_IRQ], 2U);
	CHECK_EQ("a normal-mode stream disables itself", dma(S6CR) & EN, 0U);

	now = 3U * FRAME;
	(void)uart_isr();
	CHECK_STR("last byte on the line", line_out, "MAVL");
	CHECK_EQ("UART not done until the last frame ends", uart_isr() & TC, 0U);
	now = 4U * FRAME;
	CHECK_EQ("UART transmission complete", uart_isr() & TC, TC);
	CHECK_EQ("no further DMA interrupts", raised[DMA1_S6_IRQ], 2U);

	stm32_dma_write(dma1 + HIFCR, 4U, S6_HTIF | S6_TCIF);
	CHECK_EQ("HIFCR clears the flags", dma(HISR) & (S6_HTIF | S6_TCIF), 0U);
}

static void test_disabled_by_default(void)
{
	setup(FALSE, FALSE);
	now = 4U * FRAME;
	(void)uart_isr();
	CHECK_STR("without UART TX DMA nothing is sent", line_out, "");
	CHECK_EQ("the stream keeps its data", dma(S6NDTR), 4U);
	CHECK_EQ("no DMA interrupt", raised[DMA1_S6_IRQ], 0U);
}

static void setup_rx_circular(std_bool h7)
{
	uint32 cr = MINC | CIRC | HTIE | TCIE;

	now = 0U;
	memset(raised, 0, sizeof(raised));
	memset(ram, 0, sizeof(ram));
	stm32_dma_set_h7(h7);
	dma1 = h7 ? H7_DMA1 : F7_DMA1;
	stm32_dma_reset();
	/* H7: RM0433 table 122 selects USART2_RX (request 43) on DMAMUX1 channel
	 * 5 for DMA1 stream 5. The DMAMUX is an athrill-device model and the DMA
	 * model does not read it yet (test_h7_dmamux_routing). */
	if (!h7) {
		/* RM0410 table 27: USART2_RX is DMA1 channel 4. */
		cr |= CHSEL_4;
	}
	stm32_dma_write(dma1 + S5NDTR, 4U, 4U);
	stm32_dma_write(dma1 + S5PAR, 4U, USART2 + RDR);
	stm32_dma_write(dma1 + S5M0AR, 4U, RAM_BASE + 0x40U);
	stm32_dma_write(dma1 + S5CR, 4U, cr | EN);
}

static void test_rx_circular(std_bool h7)
{
	uint32 i;

	setup_rx_circular(h7);
	for (i = 0U; i < 2U; i++) {
		CHECK_EQ(h7 ? "H7 circular request accepted" : "F7 circular request accepted",
			stm32_dma_peripheral_rx(USART2 + RDR, 'a' + i), TRUE);
	}
	CHECK_EQ(h7 ? "H7 circular half NDTR" : "F7 circular half NDTR",
		dma(S5NDTR), 2U);
	CHECK_EQ(h7 ? "H7 circular HT flag" : "F7 circular HT flag",
		dma(HISR) & (S5_HTIF | S5_TCIF), S5_HTIF);
	CHECK_EQ(h7 ? "H7 circular HT interrupt" : "F7 circular HT interrupt",
		raised[DMA1_S5_IRQ], 1U);
	for (i = 2U; i < 4U; i++) {
		(void)stm32_dma_peripheral_rx(USART2 + RDR, 'a' + i);
	}
	CHECK_EQ(h7 ? "H7 circular full data" : "F7 circular full data",
		memcmp(&ram[0x40], "abcd", 4U), 0U);
	CHECK_EQ(h7 ? "H7 circular NDTR reload" : "F7 circular NDTR reload",
		dma(S5NDTR), 4U);
	CHECK_EQ(h7 ? "H7 circular TC and HT flags" : "F7 circular TC and HT flags",
		dma(HISR) & (S5_HTIF | S5_TCIF), S5_HTIF | S5_TCIF);
	CHECK_EQ(h7 ? "H7 circular TC interrupt" : "F7 circular TC interrupt",
		raised[DMA1_S5_IRQ], 2U);
	CHECK_EQ(h7 ? "H7 circular remains enabled" : "F7 circular remains enabled",
		dma(S5CR) & EN, EN);
	(void)stm32_dma_peripheral_rx(USART2 + RDR, 'z');
	CHECK_EQ(h7 ? "H7 circular wraps memory" : "F7 circular wraps memory",
		ram[0x40], 'z');
	stm32_dma_write(dma1 + HIFCR, 4U, S5_HTIF | S5_TCIF);
	CHECK_EQ(h7 ? "H7 circular flag clear" : "F7 circular flag clear",
		dma(HISR) & (S5_HTIF | S5_TCIF), 0U);
}

static void setup_h7_route(uint32 request)
{
	now = 0U;
	memset(raised, 0, sizeof(raised));
	memset(ram, 0, sizeof(ram));
	stm32_dma_set_h7(TRUE);
	dma1 = H7_DMA1;
	stm32_dma_reset();
	/* The request DMAMUX1 channel 0 would select for DMA1 stream 0. The DMAMUX
	 * is an athrill-device model; the DMA model routes by PAR only. */
	(void)request;
	stm32_dma_write(dma1 + S0NDTR, 4U, 1U);
	stm32_dma_write(dma1 + S0PAR, 4U, USART2 + RDR);
	stm32_dma_write(dma1 + S0M0AR, 4U, RAM_BASE + 0x20U);
	stm32_dma_write(dma1 + S0CR, 4U, MINC | TCIE | EN);
}

/*
 * Known defect, kept as WILL_FAIL: RM0433 17.4.4 says DMAREQ_ID=0 selects
 * no line, and table 122 assigns USART2_RX request 43. The model currently
 * routes solely by PAR, ignoring the channel's DMAMUX selection. When the DMA
 * moves to athrill-device (hakoniwalab/athrill-device#13 step 4-5), it reads
 * the selection from the DMAMUX model and this test goes with it.
 */
static void test_h7_dmamux_routing(void)
{
	setup_h7_route(0U);
	CHECK_EQ("H7 no DMAMUX request selected", stm32_dma_peripheral_rx(
		USART2 + RDR, 'n'), FALSE);
	CHECK_EQ("H7 no route keeps NDTR", dma(S0NDTR), 1U);
	CHECK_EQ("H7 no route keeps memory", ram[0x20], 0U);

	setup_h7_route(H7_USART2_TX_REQUEST);
	CHECK_EQ("H7 wrong DMAMUX request rejected", stm32_dma_peripheral_rx(
		USART2 + RDR, 'w'), FALSE);
	CHECK_EQ("H7 wrong route keeps NDTR", dma(S0NDTR), 1U);

	setup_h7_route(H7_USART2_RX_REQUEST);
	CHECK_EQ("H7 USART2_RX route accepted", stm32_dma_peripheral_rx(
		USART2 + RDR, 'r'), TRUE);
	CHECK_EQ("H7 routed byte reaches memory", ram[0x20], 'r');
	CHECK_EQ("H7 routed transfer NDTR", dma(S0NDTR), 0U);
	CHECK_EQ("H7 routed transfer TC flag", dma(LISR) & S0_TCIF, S0_TCIF);
	CHECK_EQ("H7 routed transfer interrupt", raised[DMA1_S0_IRQ], 1U);
}

int main(int argc, char **argv)
{
	if ((argc == 2) && (strcmp(argv[1], "dmamux-spec-gap") == 0)) {
		test_h7_dmamux_routing();
		printf("%d checks, %d failures\n", checks, failures);
		return (failures == 0) ? 0 : 1;
	}
	test_four_bytes(FALSE);
	test_four_bytes(TRUE);
	test_disabled_by_default();
	test_rx_circular(FALSE);
	test_rx_circular(TRUE);
	printf("%d checks, %d failures\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}

/*
 * Unit tests of the STM32F7 USART model (src/device/peripheral/stm32_uart.c)
 * against the behaviour RM0410 describes, with the CPU clock, the NVIC and
 * the console stubbed. Frame times are worked out by hand: USART3 runs on
 * APB1 (4 CPU clocks per kernel clock), USART1 on APB2 (2).
 */
#include <stdio.h>
#include <string.h>
#include "peripheral/stm32_uart.h"
#include "peripheral/stm32_dma.h"
#include "device.h"
#include "concrete_executor/target/dbg_target_serial.h"

/* stubs of the emulator */
static uint64 now;
static uint32 raised[128];
static uint8 active_irq[128];
static char console_out[256];
static uint32 console_len;
std_bool stm32_device_accessed;

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
	return active_irq[intno] != 0U;
}

/* the DMA stream reading RDR: takes the characters when dma_enabled */
static std_bool dma_enabled;
static uint32 dma_par;
static char dma_got[16];
static uint32 dma_len;
static const char *dma_tx_data;
static uint32 dma_tx_pos;

std_bool stm32_dma_peripheral_rx(uint32 par, uint32 data)
{
	if (!dma_enabled) {
		return FALSE;
	}
	dma_par = par;
	if (dma_len < (sizeof(dma_got) - 1U)) {
		dma_got[dma_len++] = (char)data;
		dma_got[dma_len] = '\0';
	}
	return TRUE;
}

std_bool stm32_dma_peripheral_tx(uint32 par, uint32 *data)
{
	(void)par;
	if ((dma_tx_data == NULL) || (dma_tx_data[dma_tx_pos] == '\0')) {
		return FALSE;
	}
	*data = (uint8)dma_tx_data[dma_tx_pos++];
	return TRUE;
}

bool dbg_serial_putchar(uint8 c, uint8 d)
{
	(void)c;
	if (console_len < (sizeof(console_out) - 1U)) {
		console_out[console_len++] = (char)d;
		console_out[console_len] = '\0';
	}
	return TRUE;
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

#define USART1	0x40011000U
#define USART2	0x40004400U
#define USART3	0x40004800U

#define CR1	0x00U
#define CR2	0x04U
#define CR3	0x08U
#define BRR	0x0CU
#define RQR	0x18U
#define ISR	0x1CU
#define ICR	0x20U
#define RDR	0x24U
#define TDR	0x28U

#define UE	(1U << 0)
#define RE	(1U << 2)
#define TE	(1U << 3)
#define RXNEIE	(1U << 5)
#define TCIE	(1U << 6)
#define TXEIE	(1U << 7)
#define RXNE	(1U << 5)
#define TC	(1U << 6)
#define TXE	(1U << 7)

/* 54 MHz / 469: 115200 baud; 10 bits of 469 kernel clocks of 4 CPU clocks */
#define FRAME_115200	(10U * 469U * 4U)

static void setup(const char *rx_file)
{
	uint32 i;

	now = 0U;
	for (i = 0U; i < 128U; i++) {
		raised[i] = 0U;
		active_irq[i] = 0U;
	}
	console_len = 0U;
	console_out[0] = '\0';
	dma_tx_data = NULL;
	dma_tx_pos = 0U;
	stm32_uart_set_tx_dma(TRUE);
	stm32_uart_reset(3U, rx_file, NULL);
}

static void wr(uint32 base, uint32 offset, uint32 value)
{
	stm32_uart_write(base + offset, 4U, value);
}

static uint32 rd(uint32 base, uint32 offset)
{
	return stm32_uart_read(base + offset);
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

static void test_transmit(void)
{
	setup(NULL);
	CHECK_EQ("reset: TXE and TC", rd(USART3, ISR), TXE | TC);
	wr(USART3, TDR, 'x');
	CHECK_STR("disabled transmitter sends nothing", console_out, "");
	wr(USART3, BRR, 469U);
	wr(USART3, CR1, UE | TE);
	CHECK_EQ("TEACK follows TE", rd(USART3, ISR) & (1U << 21), 1U << 21);
	wr(USART3, TDR, 'A');
	CHECK_STR("the first character goes straight to the shift register", console_out, "A");
	CHECK_EQ("TDR empty again, TC cleared", rd(USART3, ISR) & (TXE | TC), TXE);
	wr(USART3, TDR, 'B');
	CHECK_EQ("TDR full: TXE 0", rd(USART3, ISR) & TXE, 0U);
	CHECK_STR("B waits in TDR", console_out, "A");
	now = FRAME_115200 - 1U;
	CHECK_EQ("A still being sent", rd(USART3, ISR) & TXE, 0U);
	now = FRAME_115200;
	CHECK_EQ("A sent: B into the shift register, TXE", rd(USART3, ISR) & (TXE | TC), TXE);
	CHECK_STR("B sent on", console_out, "AB");
	now = 2U * FRAME_115200;
	CHECK_EQ("B sent: TC", rd(USART3, ISR) & (TXE | TC), TXE | TC);
	wr(USART3, ICR, 1U << 6);
	CHECK_EQ("ICR TCCF clears TC", rd(USART3, ISR) & TC, 0U);
	/* writing TDR when full overwrites it */
	wr(USART3, TDR, 'C');
	wr(USART3, TDR, 'D');
	wr(USART3, TDR, 'E');
	now = 3U * FRAME_115200;
	(void)rd(USART3, ISR);
	CHECK_STR("D overwritten by E", console_out, "ABCE");
	/* UE = 0 resets the transmitter */
	wr(USART3, TDR, 'F');
	CHECK_EQ("F waits", rd(USART3, ISR) & TXE, 0U);
	wr(USART3, CR1, 0U);
	CHECK_EQ("UE 0: TDR emptied, TC", rd(USART3, ISR) & (TXE | TC), TXE | TC);
	now = 5U * FRAME_115200;
	(void)rd(USART3, ISR);
	CHECK_STR("F dropped", console_out, "ABCE");

	/* DMAT: TXE requests fetch one byte into the shifter and one into TDR. */
	setup(NULL);
	wr(USART3, BRR, 469U);
	wr(USART3, CR1, UE | TE);
	dma_tx_data = "XY";
	wr(USART3, CR3, 1U << 7);
	(void)tick();
	CHECK_STR("UART DMA starts at TXE", console_out, "X");
	CHECK_EQ("UART DMA has filled TDR", rd(USART3, ISR) & TXE, 0U);
	now = FRAME_115200;
	(void)tick();
	CHECK_STR("UART DMA follows the frame time", console_out, "XY");

	/* a UART other than the console: an unconnected line */
	setup(NULL);
	wr(USART2, BRR, 469U);
	wr(USART2, CR1, UE | TE);
	wr(USART2, TDR, 'Z');
	wr(USART2, TDR, 'Y');
	CHECK_EQ("USART2 transmits (TXE 0)", rd(USART2, ISR) & TXE, 0U);
	now = 2U * FRAME_115200;
	CHECK_EQ("USART2: TC", rd(USART2, ISR) & TC, TC);
	CHECK_STR("USART2 is not the console", console_out, "");
}

static uint64 frame_of(uint32 base, uint32 cr1, uint32 cr2, uint32 brr)
{
	setup(NULL);
	wr(base, BRR, brr);
	wr(base, CR2, cr2);
	wr(base, CR1, UE | TE | cr1);
	wr(base, TDR, 'a');
	wr(base, TDR, 'b');
	for (now = 1U; now < 1000000U; now++) {
		if ((rd(base, ISR) & TXE) != 0U) {
			return now;
		}
	}
	return 0U;
}

static void test_frame_formats(void)
{
	CHECK_EQ("8N1", frame_of(USART3, 0U, 0U, 469U), 10U * 469U * 4U);
	CHECK_EQ("7 data bits (M1)", frame_of(USART3, 1U << 28, 0U, 469U), 9U * 469U * 4U);
	CHECK_EQ("9 data bits (M0)", frame_of(USART3, 1U << 12, 0U, 469U), 11U * 469U * 4U);
	CHECK_EQ("2 stop bits", frame_of(USART3, 0U, 2U << 12, 469U), 11U * 469U * 4U);
	CHECK_EQ("1.5 stop bits", frame_of(USART3, 0U, 3U << 12, 469U), (21U * 469U * 4U) / 2U);
	/* OVER8: USARTDIV = 0x1D0 | (5 << 1) = 474, a bit is 474 / 2 kernel clocks */
	CHECK_EQ("OVER8", frame_of(USART3, 1U << 15, 0U, 0x1D5U), 10U * 237U * 4U);
	/* USART1 on APB2: 108 MHz / 938 */
	CHECK_EQ("USART1 on APB2", frame_of(USART1, 0U, 0U, 938U), 10U * 938U * 2U);
}

static void test_transmit_interrupts(void)
{
	uint64 until;

	setup(NULL);
	wr(USART3, BRR, 469U);
	wr(USART3, CR1, UE | TE | TXEIE);
	(void)tick();
	CHECK_EQ("TXEIE with TDR empty: IRQ 39", raised[39], 1U);
	active_irq[39] = 1U;
	wr(USART3, TDR, 'A');
	wr(USART3, TDR, 'B');
	until = tick();
	CHECK_EQ("TDR full: no interrupt", raised[39], 1U);
	CHECK_EQ("clocks to TDR empty", until, FRAME_115200);
	now = FRAME_115200;
	(void)tick();
	CHECK_EQ("TXE rises: interrupt even while active", raised[39], 2U);
	(void)tick();
	CHECK_EQ("a high line while active: not again", raised[39], 2U);
	active_irq[39] = 0U;
	(void)tick();
	CHECK_EQ("a high line, handler done: pended again", raised[39], 3U);
	/* TCIE: the clocks to the end of the last frame */
	wr(USART3, CR1, UE | TE | TCIE);
	until = tick();
	CHECK_EQ("clocks to TC", until, FRAME_115200);
	now = 2U * FRAME_115200;
	(void)tick();
	CHECK_EQ("TC interrupt", raised[39], 4U);
	wr(USART3, CR1, TE | TCIE);
	raised[39] = 0U;
	(void)tick();
	CHECK_EQ("UE 0: no interrupt", raised[39], 0U);
}

static void test_receive(void)
{
	uint64 until;

	setup(NULL);
	(void)stm32_uart_console_input('h');
	(void)stm32_uart_console_input('i');
	wr(USART3, BRR, 469U);
	wr(USART3, CR1, UE | RE);
	CHECK_EQ("REACK follows RE", rd(USART3, ISR) & (1U << 22), 1U << 22);
	CHECK_EQ("no reader (RXNEIE, DMAR): no input", rd(USART3, ISR) & RXNE, 0U);
	wr(USART3, CR1, UE | RE | RXNEIE);
	until = tick();
	CHECK_EQ("a reader: the first character", rd(USART3, ISR) & RXNE, RXNE);
	CHECK_EQ("RXNE interrupt", raised[39], 1U);
	CHECK_EQ("RDR full: no interval for input", until, DEVICE_CLOCK_MAX_INTERVAL);
	now = 100U;
	CHECK_EQ("RDR", rd(USART3, RDR), 'h');
	CHECK_EQ("reading RDR clears RXNE", rd(USART3, ISR) & RXNE, 0U);
	until = tick();
	CHECK_EQ("clocks to the next character", until, FRAME_115200 - 100U);
	now = FRAME_115200 - 1U;
	CHECK_EQ("not yet", rd(USART3, ISR) & RXNE, 0U);
	now = FRAME_115200;
	CHECK_EQ("second character", rd(USART3, ISR) & RXNE, RXNE);
	wr(USART3, RQR, 1U << 3);
	CHECK_EQ("RQR RXFRQ drops it", rd(USART3, ISR) & RXNE, 0U);
	CHECK_EQ("RDR keeps the last character", rd(USART3, RDR), 'i');
	/*
	 * waiting for RDR: the next frame was received meanwhile and enters RDR
	 * when it is read; the one after comes a frame later
	 */
	(void)stm32_uart_console_input('1');
	(void)stm32_uart_console_input('2');
	(void)stm32_uart_console_input('3');
	now = 10U * FRAME_115200;
	CHECK_EQ("1 arrives", rd(USART3, RDR), '1');
	CHECK_EQ("2 was waiting: at once", rd(USART3, RDR), '2');
	now += FRAME_115200 - 1U;
	CHECK_EQ("3 one frame later, not before", rd(USART3, ISR) & RXNE, 0U);
	now += 1U;
	CHECK_EQ("3 arrives", rd(USART3, RDR), '3');
	/* DMAR is a reader too */
	setup(NULL);
	(void)stm32_uart_console_input('d');
	wr(USART3, CR3, 1U << 6);
	wr(USART3, CR1, UE | RE);
	CHECK_EQ("DMAR, no stream enabled: input in RDR", rd(USART3, RDR), 'd');
	/* DMAR with an enabled stream: the stream takes each character a frame apart, RDR stays empty */
	setup(NULL);
	dma_enabled = TRUE;
	dma_len = 0U;
	(void)stm32_uart_console_input('x');
	(void)stm32_uart_console_input('y');
	wr(USART3, BRR, 469U);
	wr(USART3, CR3, 1U << 6);
	wr(USART3, CR1, UE | RE);
	CHECK_EQ("DMA: clocks to the next character", tick(), FRAME_115200);
	CHECK_STR("DMA: the first character", dma_got, "x");
	CHECK_EQ("DMA: from RDR", dma_par, USART3 + RDR);
	CHECK_EQ("DMA: RXNE not set", rd(USART3, ISR) & RXNE, 0U);
	now = FRAME_115200;
	(void)tick();
	CHECK_STR("DMA: the second a frame later", dma_got, "xy");
	/* after an idle line the next character comes a frame after the one that arrived, not at every tick */
	(void)stm32_uart_console_input('1');
	(void)stm32_uart_console_input('2');
	now = 100U * FRAME_115200;
	(void)tick();
	now += 1U;
	(void)tick();
	CHECK_STR("DMA: one character, not a burst", dma_got, "xy1");
	now += FRAME_115200 - 1U;
	(void)tick();
	CHECK_STR("DMA: the next a frame later", dma_got, "xy12");
	dma_enabled = FALSE;
	/* only the console receives */
	setup(NULL);
	(void)stm32_uart_console_input('q');
	wr(USART2, CR1, UE | RE | RXNEIE);
	CHECK_EQ("USART2 receives nothing", rd(USART2, ISR) & RXNE, 0U);
}

static void test_receive_file(void)
{
	const char *path = "test_stm32_uart_rx.txt";
	FILE *f = fopen(path, "wb");

	fputs("ab", f);
	fclose(f);
	setup(path);
	wr(USART3, BRR, 469U);
	wr(USART3, CR1, UE | RE | RXNEIE);
	CHECK_EQ("file: a", rd(USART3, RDR), 'a');
	now = FRAME_115200;
	CHECK_EQ("file: b a frame later", rd(USART3, RDR), 'b');
	now = 2U * FRAME_115200;
	CHECK_EQ("file: end, nothing", rd(USART3, ISR) & RXNE, 0U);
	CHECK_EQ("file end: polled again later", tick(), 1000000U);
	f = fopen(path, "ab");
	fputs("c", f);
	fclose(f);
	now = (2U * FRAME_115200) + 999999U;
	CHECK_EQ("appended: not before the poll", rd(USART3, ISR) & RXNE, 0U);
	now += 1U;
	CHECK_EQ("appended: c", rd(USART3, RDR), 'c');
	remove(path);
}

static void test_transmit_file(void)
{
	const char *path = "test_stm32_uart_tx.bin";
	char got[3] = { 0 };
	FILE *f;

	setup(NULL);
	stm32_uart_reset(3U, NULL, path);
	wr(USART3, BRR, 469U);
	wr(USART3, CR1, UE | TE);
	wr(USART3, TDR, 'A');
	wr(USART3, TDR, 'B');
	now = FRAME_115200;
	(void)rd(USART3, ISR);
	stm32_uart_reset(3U, NULL, NULL); /* closes and flushes the capture */
	f = fopen(path, "rb");
	CHECK_EQ("TX file opened", f != NULL, 1U);
	if (f != NULL) {
		CHECK_EQ("TX file bytes", fread(got, 1U, 2U, f), 2U);
		fclose(f);
	}
	CHECK_STR("TX file is raw UART data", got, "AB");
	remove(path);
}

int main(void)
{
	test_transmit();
	test_frame_formats();
	test_transmit_interrupts();
	test_receive();
	test_receive_file();
	test_transmit_file();
	printf("%d checks, %d failures\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}

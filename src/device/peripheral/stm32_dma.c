/*
 * The STM32F7/H7 DMA controllers (see stm32_dma.h). Offsets and bits from
 * RM0410 8.5 and RM0433 16.5.
 */
#include "stm32_dma.h"
#include "stm32_spi.h"
#include "bus.h"
#include "cpu.h"
#include "std_device_ops.h"

#define F7_DMA1_BASE	0x40026000U
#define F7_DMA2_BASE	0x40026400U
#define H7_DMA1_BASE	0x40020000U
#define H7_DMA2_BASE	0x40020400U
#define DMA_SIZE	0x400U

#define DMA_LISR	0x00U
#define DMA_HISR	0x04U
#define DMA_LIFCR	0x08U
#define DMA_HIFCR	0x0CU
#define DMA_STREAM0	0x10U
#define DMA_STREAM_SIZE	0x18U
#define STREAMS		8U

#define SxCR		0U
#define SxNDTR		1U
#define SxPAR		2U
#define SxM0AR		3U
#define SxM1AR		4U
#define SxFCR		5U

#define CR_EN		(1U << 0)
#define CR_HTIE		(1U << 3)
#define CR_TCIE		(1U << 4)
#define CR_DIR_SHIFT	6U
#define CR_CIRC		(1U << 8)
#define CR_PINC		(1U << 9)
#define CR_MINC		(1U << 10)
#define CR_PSIZE_SHIFT	11U
#define CR_MSIZE_SHIFT	13U
#define DIR_P2M		0U
#define DIR_M2P		1U

#define FLAG_HT		(1U << 4)
#define FLAG_TC		(1U << 5)

#define SPI_DR_OFFSET	0x0CU

typedef struct {
	uint32 isr[2];		/* LISR, HISR */
	uint32 stream[STREAMS][6];
	uint32 ndtr_start[STREAMS];	/* NDTR when the stream was enabled (circular reload) */
	uint32 irq[STREAMS];
} DmaType;

static DmaType dmas[2] = {
	{ .irq = { 11U, 12U, 13U, 14U, 15U, 16U, 17U, 47U } },
	{ .irq = { 56U, 57U, 58U, 59U, 60U, 68U, 69U, 70U } },
};

static uint32 dma1_base = F7_DMA1_BASE;
static uint32 dma2_base = F7_DMA2_BASE;

void stm32_dma_set_h7(std_bool h7)
{
	dma1_base = h7 ? H7_DMA1_BASE : F7_DMA1_BASE;
	dma2_base = h7 ? H7_DMA2_BASE : F7_DMA2_BASE;
}

std_bool stm32_dma_owns(uint32 addr)
{
	return (addr >= dma1_base) && (addr < (dma2_base + DMA_SIZE));
}

/* The bit position of stream s's flags in LISR/HISR. */
static uint32 flag_shift(uint32 s)
{
	static const uint32 shifts[4] = { 0U, 6U, 16U, 22U };

	return shifts[s % 4U];
}

uint32 stm32_dma_read(uint32 addr)
{
	DmaType *d = &dmas[(addr >= dma2_base) ? 1 : 0];
	uint32 offset = (addr - ((addr >= dma2_base) ? dma2_base : dma1_base)) & ~3U;
	uint32 shift = 8U * (addr & 3U);

	if (offset <= DMA_HISR) {
		return d->isr[offset / 4U] >> shift;
	}
	if ((offset >= DMA_STREAM0) && (offset < (DMA_STREAM0 + (STREAMS * DMA_STREAM_SIZE)))) {
		uint32 s = (offset - DMA_STREAM0) / DMA_STREAM_SIZE;
		uint32 r = ((offset - DMA_STREAM0) % DMA_STREAM_SIZE) / 4U;

		return d->stream[s][r] >> shift;
	}
	return 0U;
}

/* A stream has moved all its data: transfer complete (and half transfer), disabled unless circular. */
static void complete(DmaType *d, uint32 s)
{
	uint32 *st = d->stream[s];

	d->isr[s / 4U] |= (FLAG_TC | FLAG_HT) << flag_shift(s);
	st[SxNDTR] = 0U;
	if ((st[SxCR] & CR_CIRC) == 0U) {
		st[SxCR] &= ~CR_EN;
	}
	if ((st[SxCR] & (CR_TCIE | CR_HTIE)) != 0U) {
		(void)intc_raise_intr(d->irq[s]);
	}
}

static uint32 unit(uint32 cr, uint32 shift)
{
	return 1U << ((cr >> shift) & 3U);	/* bytes: 1, 2, 4 */
}

static uint32 mem_read(uint32 addr, uint32 size)
{
	uint8 v8 = 0U;
	uint16 v16 = 0U;
	uint32 v32 = 0U;

	if (size == 1U) {
		(void)bus_get_data8(0U, addr, &v8);
		return v8;
	}
	if (size == 2U) {
		(void)bus_get_data16(0U, addr, &v16);
		return v16;
	}
	(void)bus_get_data32(0U, addr, &v32);
	return v32;
}

static void mem_write(uint32 addr, uint32 size, uint32 value)
{
	if (size == 1U) {
		(void)bus_put_data8(0U, addr, (uint8)value);
	}
	else if (size == 2U) {
		(void)bus_put_data16(0U, addr, (uint16)value);
	}
	else {
		(void)bus_put_data32(0U, addr, value);
	}
}

/* The enabled stream that reads from peripheral address par, if any. */
static std_bool find_rx(uint32 par, DmaType **rd, uint32 *rs)
{
	uint32 c;
	uint32 s;

	for (c = 0U; c < 2U; c++) {
		for (s = 0U; s < STREAMS; s++) {
			uint32 *st = dmas[c].stream[s];

			if (((st[SxCR] & CR_EN) != 0U) && (((st[SxCR] >> CR_DIR_SHIFT) & 3U) == DIR_P2M)
				&& (st[SxPAR] == par)) {
				*rd = &dmas[c];
				*rs = s;
				return TRUE;
			}
		}
	}
	return FALSE;
}

/* The enabled stream that writes to peripheral address par, if any. */
static std_bool find_tx(uint32 par, DmaType **td, uint32 *ts)
{
	uint32 c;
	uint32 s;

	for (c = 0U; c < 2U; c++) {
		for (s = 0U; s < STREAMS; s++) {
			uint32 *st = dmas[c].stream[s];

			if (((st[SxCR] & CR_EN) != 0U) && (((st[SxCR] >> CR_DIR_SHIFT) & 3U) == DIR_M2P)
				&& (st[SxPAR] == par)) {
				*td = &dmas[c];
				*ts = s;
				return TRUE;
			}
		}
	}
	return FALSE;
}

/* TX stream s enabled: an SPI exchange of NDTR frames, with the RX stream of the same DR. */
static void start(DmaType *d, uint32 s)
{
	uint32 *tx = d->stream[s];
	uint32 par = tx[SxPAR];
	DmaType *rd = NULL;
	uint32 rs = 0U;
	uint32 *rx = NULL;
	uint32 n = tx[SxNDTR];
	uint32 tx_size;
	uint32 rx_size = 1U;
	uint32 i;

	if ((((tx[SxCR] >> CR_DIR_SHIFT) & 3U) != DIR_M2P) || !stm32_spi_owns(par)
		|| ((par & 0x3FFU) != SPI_DR_OFFSET)) {
		return;
	}
	if (find_rx(par, &rd, &rs)) {
		rx = rd->stream[rs];
		rx_size = unit(rx[SxCR], CR_MSIZE_SHIFT);
	}
	tx_size = unit(tx[SxCR], CR_MSIZE_SHIFT);
	for (i = 0U; i < n; i++) {
		uint32 from = tx[SxM0AR] + (((tx[SxCR] & CR_MINC) != 0U) ? (i * tx_size) : 0U);

		stm32_spi_write(par, unit(tx[SxCR], CR_PSIZE_SHIFT), mem_read(from, tx_size));
		if (rx != NULL) {
			uint32 to = rx[SxM0AR] + (((rx[SxCR] & CR_MINC) != 0U) ? (i * rx_size) : 0U);

			mem_write(to, rx_size, stm32_spi_read(par, unit(rx[SxCR], CR_PSIZE_SHIFT)));
		}
	}
	if (rx != NULL) {
		complete(rd, rs);
	}
	complete(d, s);
}

void stm32_dma_write(uint32 addr, uint32 size, uint32 value)
{
	DmaType *d = &dmas[(addr >= dma2_base) ? 1 : 0];
	uint32 offset = (addr - ((addr >= dma2_base) ? dma2_base : dma1_base)) & ~3U;
	uint32 shift = 8U * (addr & 3U);
	uint32 mask = ((size >= 4U) ? 0xFFFFFFFFU : ((1U << (8U * size)) - 1U)) << shift;

	value = (value << shift) & mask;
	if ((offset == DMA_LIFCR) || (offset == DMA_HIFCR)) {
		d->isr[(offset - DMA_LIFCR) / 4U] &= ~value;
		return;
	}
	if ((offset >= DMA_STREAM0) && (offset < (DMA_STREAM0 + (STREAMS * DMA_STREAM_SIZE)))) {
		uint32 s = (offset - DMA_STREAM0) / DMA_STREAM_SIZE;
		uint32 r = ((offset - DMA_STREAM0) % DMA_STREAM_SIZE) / 4U;
		uint32 was = d->stream[s][SxCR];

		d->stream[s][r] = (d->stream[s][r] & ~mask) | value;
		if (r == SxNDTR) {
			d->stream[s][r] &= 0xFFFFU;
		}
		if ((r == SxCR) && ((was & CR_EN) == 0U) && ((d->stream[s][SxCR] & CR_EN) != 0U)) {
			d->ndtr_start[s] = d->stream[s][SxNDTR];
			start(d, s);
		}
	}
}

std_bool stm32_dma_peripheral_rx(uint32 par, uint32 data)
{
	DmaType *d = NULL;
	uint32 s = 0U;
	uint32 *st;
	uint32 size;
	uint32 done;
	uint32 flags = 0U;

	if (!find_rx(par, &d, &s)) {
		return FALSE;
	}
	st = d->stream[s];
	if (st[SxNDTR] == 0U) {
		return FALSE;
	}
	size = unit(st[SxCR], CR_MSIZE_SHIFT);
	done = d->ndtr_start[s] - st[SxNDTR];
	mem_write(st[SxM0AR] + (((st[SxCR] & CR_MINC) != 0U) ? (done * size) : 0U), size, data);
	st[SxNDTR]--;
	done++;
	if (done == (d->ndtr_start[s] / 2U)) {
		flags = FLAG_HT;
	}
	if (st[SxNDTR] == 0U) {
		flags = FLAG_TC;
		if ((st[SxCR] & CR_CIRC) != 0U) {
			st[SxNDTR] = d->ndtr_start[s];
		}
		else {
			st[SxCR] &= ~CR_EN;
		}
	}
	d->isr[s / 4U] |= flags << flag_shift(s);
	if ((((flags & FLAG_HT) != 0U) && ((st[SxCR] & CR_HTIE) != 0U))
		|| (((flags & FLAG_TC) != 0U) && ((st[SxCR] & CR_TCIE) != 0U))) {
		(void)intc_raise_intr(d->irq[s]);
	}
	return TRUE;
}

std_bool stm32_dma_peripheral_tx(uint32 par, uint32 *data)
{
	DmaType *d = NULL;
	uint32 s = 0U;
	uint32 *st;
	uint32 size;
	uint32 done;
	uint32 flags = 0U;

	if (!find_tx(par, &d, &s)) {
		return FALSE;
	}
	st = d->stream[s];
	if (st[SxNDTR] == 0U) {
		return FALSE;
	}
	size = unit(st[SxCR], CR_MSIZE_SHIFT);
	done = d->ndtr_start[s] - st[SxNDTR];
	*data = mem_read(st[SxM0AR] + (((st[SxCR] & CR_MINC) != 0U) ? (done * size) : 0U), size);
	st[SxNDTR]--;
	done++;
	if (done == (d->ndtr_start[s] / 2U)) {
		flags = FLAG_HT;
	}
	if (st[SxNDTR] == 0U) {
		flags = FLAG_TC;
		if ((st[SxCR] & CR_CIRC) != 0U) {
			st[SxNDTR] = d->ndtr_start[s];
		}
		else {
			st[SxCR] &= ~CR_EN;
		}
	}
	d->isr[s / 4U] |= flags << flag_shift(s);
	if ((((flags & FLAG_HT) != 0U) && ((st[SxCR] & CR_HTIE) != 0U))
		|| (((flags & FLAG_TC) != 0U) && ((st[SxCR] & CR_TCIE) != 0U))) {
		(void)intc_raise_intr(d->irq[s]);
	}
	return TRUE;
}

std_bool stm32_dma_peripheral_ready(uint32 par, std_bool to_memory)
{
	DmaType *d = NULL;
	uint32 s = 0U;

	return to_memory ? find_rx(par, &d, &s) : find_tx(par, &d, &s);
}

std_bool stm32_dma_peripheral_block_rx(uint32 par, const uint8 *data, uint32 len)
{
	DmaType *d = NULL;
	uint32 s = 0U;
	uint32 i;

	if (!find_rx(par, &d, &s)) {
		return FALSE;
	}
	for (i = 0U; i < len; i++) {
		mem_write(d->stream[s][SxM0AR] + i, 1U, data[i]);
	}
	complete(d, s);
	return TRUE;
}

std_bool stm32_dma_peripheral_block_tx(uint32 par, uint8 *data, uint32 len)
{
	DmaType *d = NULL;
	uint32 s = 0U;
	uint32 i;

	if (!find_tx(par, &d, &s)) {
		return FALSE;
	}
	for (i = 0U; i < len; i++) {
		data[i] = (uint8)mem_read(d->stream[s][SxM0AR] + i, 1U);
	}
	complete(d, s);
	return TRUE;
}

void stm32_dma_reset(void)
{
	uint32 c;
	uint32 s;
	uint32 r;

	for (c = 0U; c < 2U; c++) {
		dmas[c].isr[0] = 0U;
		dmas[c].isr[1] = 0U;
		for (s = 0U; s < STREAMS; s++) {
			for (r = 0U; r < 6U; r++) {
				dmas[c].stream[s][r] = 0U;
			}
			dmas[c].stream[s][SxFCR] = 0x21U;
			dmas[c].ndtr_start[s] = 0U;
		}
	}
}

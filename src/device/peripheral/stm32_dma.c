/*
 * The STM32F7 DMA controllers (see stm32_dma.h). Offsets and bits from
 * RM0410 8.5.
 */
#include "stm32_dma.h"
#include "stm32_spi.h"
#include "bus.h"
#include "cpu.h"
#include "std_device_ops.h"

#define DMA1_BASE	0x40026000U
#define DMA2_BASE	0x40026400U
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
	uint32 irq[STREAMS];
} DmaType;

static DmaType dmas[2] = {
	{ .irq = { 11U, 12U, 13U, 14U, 15U, 16U, 17U, 47U } },
	{ .irq = { 56U, 57U, 58U, 59U, 60U, 68U, 69U, 70U } },
};

std_bool stm32_dma_owns(uint32 addr)
{
	return (addr >= DMA1_BASE) && (addr < (DMA2_BASE + DMA_SIZE));
}

/* The bit position of stream s's flags in LISR/HISR. */
static uint32 flag_shift(uint32 s)
{
	static const uint32 shifts[4] = { 0U, 6U, 16U, 22U };

	return shifts[s % 4U];
}

uint32 stm32_dma_read(uint32 addr)
{
	DmaType *d = &dmas[(addr >= DMA2_BASE) ? 1 : 0];
	uint32 offset = (addr - ((addr >= DMA2_BASE) ? DMA2_BASE : DMA1_BASE)) & ~3U;
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
	DmaType *d = &dmas[(addr >= DMA2_BASE) ? 1 : 0];
	uint32 offset = (addr - ((addr >= DMA2_BASE) ? DMA2_BASE : DMA1_BASE)) & ~3U;
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
			start(d, s);
		}
	}
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
		}
	}
}

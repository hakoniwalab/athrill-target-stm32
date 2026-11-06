/*
 * STM32H743 SPI-v2 controllers (RM0433 chapter 52).  This is only the H7
 * register/FIFO front end; the bus, chip-select and chip implementations are
 * shared with the F7 front end in stm32_spi.c.
 */
#include "stm32h7_spi.h"
#include "stm32_spi.h"

#define SPI_CR1		0x00U
#define SPI_CFG1	0x08U
#define SPI_SR		0x14U
#define SPI_IFCR	0x18U
#define SPI_TXDR	0x20U
#define SPI_RXDR	0x30U
#define SPI_REGS	0x50U
#define SPI_BLOCK	0x400U

#define CR1_SPE		(1U << 0)
#define CR1_CSTART	(1U << 9)
#define CR1_CSUSP	(1U << 10)
#define SR_RXP		(1U << 0)
#define SR_TXP		(1U << 1)
#define SR_EOT		(1U << 3)
#define SR_SUSP		(1U << 11)
#define SR_TXC		(1U << 12)
#define RX_FIFO_SIZE	16U

typedef struct {
	uint32 base;
	uint32 bus;
	uint32 regs[SPI_REGS / 4U];
	uint8 rx[RX_FIFO_SIZE];
	uint32 head;
	uint32 count;
} H7SpiType;

static H7SpiType spis[] = {
	{ .base = 0x40013000U, .bus = 1U },
	{ .base = 0x40003800U, .bus = 2U },
	{ .base = 0x40003C00U, .bus = 3U },
	{ .base = 0x40013400U, .bus = 4U },
	{ .base = 0x40015000U, .bus = 5U },
	{ .base = 0x58001400U, .bus = 6U },
};

#define SPI_NUM (sizeof(spis) / sizeof(spis[0]))

static H7SpiType *spi_of(uint32 addr)
{
	uint32 i;

	for (i = 0U; i < SPI_NUM; i++) {
		if ((addr >= spis[i].base) && (addr < (spis[i].base + SPI_BLOCK))) {
			return &spis[i];
		}
	}
	return NULL;
}

std_bool stm32h7_spi_owns(uint32 addr)
{
	return spi_of(addr) != NULL;
}

static void push(H7SpiType *s, uint8 value)
{
	if (s->count < RX_FIFO_SIZE) {
		s->rx[(s->head + s->count) % RX_FIFO_SIZE] = value;
		s->count++;
	}
}

static uint8 pop(H7SpiType *s)
{
	uint8 value = 0xFFU;

	if (s->count != 0U) {
		value = s->rx[s->head];
		s->head = (s->head + 1U) % RX_FIFO_SIZE;
		s->count--;
	}
	return value;
}

static uint32 exchange(H7SpiType *s, uint32 size, uint32 value, std_bool queue)
{
	uint32 response = 0U;
	uint32 i;

	for (i = 0U; i < size; i++) {
		uint8 byte = stm32_spi_chip_exchange(s->bus, (uint8)(value >> (8U * i)));

		response |= (uint32)byte << (8U * i);
		if (queue) {
			push(s, byte);
		}
	}
	return response;
}

uint32 stm32h7_spi_read(uint32 addr, uint32 size)
{
	H7SpiType *s = spi_of(addr);
	uint32 offset = (addr - s->base) & ~3U;
	uint32 value = 0U;
	uint32 i;

	if (offset == SPI_SR) {
		value = SR_TXP | SR_EOT | SR_SUSP | SR_TXC;
		if (s->count != 0U) {
			value |= SR_RXP;
		}
		return value;
	}
	if (offset == SPI_RXDR) {
		for (i = 0U; i < size; i++) {
			value |= (uint32)pop(s) << (8U * i);
		}
		return value;
	}
	value = (offset < SPI_REGS) ? s->regs[offset / 4U] : 0U;
	if ((offset == SPI_CR1) && ((value & CR1_CSUSP) != 0U)) {
		value &= ~CR1_CSTART;
		s->regs[offset / 4U] = value;
	}
	return value >> (8U * ((addr - s->base) & 3U));
}

void stm32h7_spi_write(uint32 addr, uint32 size, uint32 value)
{
	H7SpiType *s = spi_of(addr);
	uint32 offset = (addr - s->base) & ~3U;
	uint32 shift = 8U * ((addr - s->base) & 3U);
	uint32 mask = ((size >= 4U) ? 0xFFFFFFFFU : ((1U << (8U * size)) - 1U)) << shift;

	if (offset == SPI_TXDR) {
		(void)exchange(s, size, value, TRUE);
		return;
	}
	if ((offset == SPI_SR) || (offset == SPI_IFCR)) {
		return;
	}
	if (offset < SPI_REGS) {
		s->regs[offset / 4U] = (s->regs[offset / 4U] & ~mask) | ((value << shift) & mask);
		if ((offset == SPI_CR1) && ((s->regs[offset / 4U] & CR1_SPE) == 0U)) {
			s->head = 0U;
			s->count = 0U;
		}
	}
}

std_bool stm32h7_spi_dma_exchange(uint32 txdr, uint32 size, uint32 value,
	uint32 *rxdr, uint32 *response)
{
	H7SpiType *s = spi_of(txdr);

	if ((s == NULL) || (((txdr - s->base) & ~3U) != SPI_TXDR)) {
		return FALSE;
	}
	*rxdr = s->base + SPI_RXDR;
	*response = exchange(s, size, value, FALSE);
	return TRUE;
}

void stm32h7_spi_reset(void)
{
	uint32 i;
	uint32 k;

	for (i = 0U; i < SPI_NUM; i++) {
		for (k = 0U; k < (SPI_REGS / 4U); k++) {
			spis[i].regs[k] = 0U;
		}
		spis[i].regs[SPI_CFG1 / 4U] = 7U; /* DSIZE: eight-bit data */
		spis[i].head = 0U;
		spis[i].count = 0U;
	}
}

/*
 * The STM32F7 SPI controllers (see stm32_spi.h). Register offsets and bits
 * from RM0410 35.9.
 */
#include "stm32_spi.h"

#define SPI_CR1		0x00U
#define SPI_CR2		0x04U
#define SPI_SR		0x08U
#define SPI_DR		0x0CU
#define SPI_CRCPR	0x10U
#define SPI_REGS	0x24U
#define SPI_BLOCK	0x400U

#define CR1_SPE		(1U << 6)
#define CR2_DS_SHIFT	8U
#define CR2_FRXTH	(1U << 12)

#define SR_RXNE		(1U << 0)
#define SR_TXE		(1U << 1)
#define SR_FRLVL_SHIFT	9U

#define RX_FIFO_SIZE	4U	/* bytes (32 bits) */
#define CHIPS_MAX	16U

typedef struct {
	uint32 base;
	uint32 regs[SPI_REGS / 4U];
	uint8 rx[RX_FIFO_SIZE];
	uint32 rx_head;
	uint32 rx_count;
} SpiType;

typedef struct {
	uint32 bus;
	uint32 port;
	uint32 pin;
	const Stm32SpiChipOps *ops;
	void *chip;
	std_bool selected;
} SpiChipType;

static SpiType spis[] = {
	{ .base = 0x40013000U },	/* SPI1 */
	{ .base = 0x40003800U },	/* SPI2 */
	{ .base = 0x40003C00U },	/* SPI3 */
	{ .base = 0x40013400U },	/* SPI4 */
	{ .base = 0x40015000U },	/* SPI5 */
	{ .base = 0x40015400U },	/* SPI6 */
};

#define SPI_NUM	(sizeof(spis) / sizeof(spis[0]))

static SpiChipType chips[CHIPS_MAX];
static uint32 chip_count;

static SpiType *spi_of(uint32 addr)
{
	uint32 i;

	for (i = 0U; i < SPI_NUM; i++) {
		if ((addr >= spis[i].base) && (addr < (spis[i].base + SPI_BLOCK))) {
			return &spis[i];
		}
	}
	return NULL;
}

std_bool stm32_spi_owns(uint32 addr)
{
	return spi_of(addr) != NULL;
}

static uint32 reg(const SpiType *s, uint32 offset)
{
	return s->regs[offset / 4U];
}

/* Bits per frame (DS + 1). */
static uint32 frame_bits(const SpiType *s)
{
	uint32 ds = (reg(s, SPI_CR2) >> CR2_DS_SHIFT) & 0xFU;

	return (ds < 3U) ? 8U : (ds + 1U);
}

/* The byte the selected chip of the bus answers to mosi (0xFF: no chip drives MISO). */
uint8 stm32_spi_chip_exchange(uint32 bus, uint8 mosi)
{
	uint32 i;

	for (i = 0U; i < chip_count; i++) {
		if ((chips[i].bus == bus) && chips[i].selected) {
			return chips[i].ops->exchange(chips[i].chip, mosi);
		}
	}
	return 0xFFU;
}

static void rx_push(SpiType *s, uint8 data)
{
	if (s->rx_count < RX_FIFO_SIZE) {
		s->rx[(s->rx_head + s->rx_count) % RX_FIFO_SIZE] = data;
		s->rx_count++;
	}
}

static uint8 rx_pop(SpiType *s)
{
	uint8 data = 0U;

	if (s->rx_count != 0U) {
		data = s->rx[s->rx_head];
		s->rx_head = (s->rx_head + 1U) % RX_FIFO_SIZE;
		s->rx_count--;
	}
	return data;
}

/* A write to DR: frames of up to 8 bits are packed two per halfword access; a wider frame goes MSB first. */
static void transmit(SpiType *s, uint32 size, uint32 value)
{
	uint32 bus = (uint32)(s - spis) + 1U;
	uint8 hi;
	uint8 lo;

	if ((reg(s, SPI_CR1) & CR1_SPE) == 0U) {
		return;
	}
	if (frame_bits(s) > 8U) {
		hi = stm32_spi_chip_exchange(bus, (uint8)(value >> 8));
		lo = stm32_spi_chip_exchange(bus, (uint8)value);
		rx_push(s, lo);
		rx_push(s, hi);
		return;
	}
	rx_push(s, stm32_spi_chip_exchange(bus, (uint8)value));
	if (size >= 2U) {
		rx_push(s, stm32_spi_chip_exchange(bus, (uint8)(value >> 8)));
	}
}

static uint32 status(const SpiType *s)
{
	uint32 sr = SR_TXE;
	uint32 needed = ((frame_bits(s) <= 8U) && ((reg(s, SPI_CR2) & CR2_FRXTH) != 0U)) ? 1U : 2U;
	uint32 level = (s->rx_count >= 3U) ? 3U : s->rx_count;

	if (s->rx_count >= needed) {
		sr |= SR_RXNE;
	}
	return sr | (level << SR_FRLVL_SHIFT);
}

uint32 stm32_spi_read(uint32 addr, uint32 size)
{
	SpiType *s = spi_of(addr);
	uint32 offset = (addr - s->base) & ~3U;
	uint32 value;

	switch (offset) {
	case SPI_SR:
		return status(s);
	case SPI_DR:
		value = rx_pop(s);
		if ((size >= 2U) || (frame_bits(s) > 8U)) {
			value |= (uint32)rx_pop(s) << 8;
		}
		return value;
	default:
		return (offset < SPI_REGS) ? (reg(s, offset) >> (8U * ((addr - s->base) & 3U))) : 0U;
	}
}

void stm32_spi_write(uint32 addr, uint32 size, uint32 value)
{
	SpiType *s = spi_of(addr);
	uint32 offset = (addr - s->base) & ~3U;
	uint32 shift = 8U * ((addr - s->base) & 3U);
	uint32 mask = ((size >= 4U) ? 0xFFFFFFFFU : ((1U << (8U * size)) - 1U)) << shift;

	switch (offset) {
	case SPI_DR:
		transmit(s, size, value);
		return;
	case SPI_SR:
		return;
	default:
		if (offset < SPI_REGS) {
			s->regs[offset / 4U] = (s->regs[offset / 4U] & ~mask) | ((value << shift) & mask);
		}
		/* SPE cleared: the FIFOs empty */
		if ((offset == SPI_CR1) && ((reg(s, SPI_CR1) & CR1_SPE) == 0U)) {
			s->rx_count = 0U;
		}
		return;
	}
}

void stm32_spi_reset(void)
{
	uint32 i;
	uint32 k;

	for (i = 0U; i < SPI_NUM; i++) {
		for (k = 0U; k < (SPI_REGS / 4U); k++) {
			spis[i].regs[k] = 0U;
		}
		spis[i].regs[SPI_CR2 / 4U] = 0x0700U;	/* 8-bit frames */
		spis[i].regs[SPI_CRCPR / 4U] = 0x0007U;
		spis[i].rx_head = 0U;
		spis[i].rx_count = 0U;
	}
	chip_count = 0U;	/* the board attaches its chips again */
}

void stm32_spi_attach(uint32 bus, uint32 port, uint32 pin, const Stm32SpiChipOps *ops, void *chip)
{
	if (chip_count >= CHIPS_MAX) {
		return;
	}
	chips[chip_count].bus = bus;
	chips[chip_count].port = port;
	chips[chip_count].pin = pin;
	chips[chip_count].ops = ops;
	chips[chip_count].chip = chip;
	chips[chip_count].selected = FALSE;
	chip_count++;
}

void stm32_spi_pins_changed(uint32 port, uint32 old, uint32 now)
{
	uint32 i;

	for (i = 0U; i < chip_count; i++) {
		SpiChipType *c = &chips[i];
		uint32 bit = 1U << c->pin;
		std_bool selected;

		if ((c->port != port) || (((old ^ now) & bit) == 0U)) {
			continue;
		}
		selected = ((now & bit) == 0U);
		if (selected != c->selected) {
			c->selected = selected;
			c->ops->select(c->chip, selected);
		}
	}
}

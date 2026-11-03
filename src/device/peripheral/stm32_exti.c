/*
 * EXTI and SYSCFG_EXTICR (see stm32_exti.h). Offsets from RM0410 11.9 and
 * 7.2.
 */
#include "stm32_exti.h"
#include "cpu.h"
#include "std_device_ops.h"

#define EXTI_BASE	0x40013C00U
#define EXTI_IMR	0x00U
#define EXTI_EMR	0x04U
#define EXTI_RTSR	0x08U
#define EXTI_FTSR	0x0CU
#define EXTI_SWIER	0x10U
#define EXTI_PR		0x14U
#define EXTI_REGS	0x18U

#define SYSCFG_EXTICR1	0x40013808U	/* to EXTICR4 at 0x40013814 */
#define EXTICR_SIZE	0x10U

#define LINES_MASK	0x01FFFFFFU	/* lines 0 to 24 */

static uint32 regs[EXTI_REGS / 4U];
static uint32 exticr[4];

static std_bool is_exticr(uint32 addr)
{
	return (addr >= SYSCFG_EXTICR1) && (addr < (SYSCFG_EXTICR1 + EXTICR_SIZE));
}

std_bool stm32_exti_owns(uint32 addr)
{
	return ((addr >= EXTI_BASE) && (addr < (EXTI_BASE + EXTI_REGS))) || is_exticr(addr);
}

/* The NVIC interrupt of a GPIO line: EXTI0..4, EXTI9_5, EXTI15_10. */
static uint32 irq_of(uint32 line)
{
	if (line <= 4U) {
		return 6U + line;
	}
	return (line <= 9U) ? 23U : 40U;
}

static void pend(uint32 lines)
{
	uint32 line;

	regs[EXTI_PR / 4U] |= lines;
	for (line = 0U; line < 16U; line++) {
		if ((((lines & regs[EXTI_IMR / 4U]) >> line) & 1U) != 0U) {
			(void)intc_raise_intr(irq_of(line));
		}
	}
}

uint32 stm32_exti_read(uint32 addr)
{
	uint32 shift = 8U * (addr & 3U);

	addr &= ~3U;
	if (is_exticr(addr)) {
		return exticr[(addr - SYSCFG_EXTICR1) / 4U] >> shift;
	}
	return regs[(addr - EXTI_BASE) / 4U] >> shift;
}

void stm32_exti_write(uint32 addr, uint32 size, uint32 value)
{
	uint32 shift = 8U * (addr & 3U);
	uint32 mask = ((size >= 4U) ? 0xFFFFFFFFU : ((1U << (8U * size)) - 1U)) << shift;
	uint32 offset;

	value = (value << shift) & mask;
	addr &= ~3U;
	if (is_exticr(addr)) {
		uint32 *r = &exticr[(addr - SYSCFG_EXTICR1) / 4U];

		*r = ((*r & ~mask) | value) & 0xFFFFU;
		return;
	}
	offset = addr - EXTI_BASE;
	switch (offset) {
	case EXTI_PR:	/* rc_w1 */
		regs[EXTI_PR / 4U] &= ~value;
		break;
	case EXTI_SWIER:	/* a software interrupt on each line written 1 */
		pend(value & LINES_MASK);
		break;
	default:
		regs[offset / 4U] = ((regs[offset / 4U] & ~mask) | value) & LINES_MASK;
		break;
	}
}

void stm32_exti_reset(void)
{
	uint32 i;

	for (i = 0U; i < (EXTI_REGS / 4U); i++) {
		regs[i] = 0U;
	}
	for (i = 0U; i < 4U; i++) {
		exticr[i] = 0U;
	}
}

void stm32_exti_pin_edge(uint32 port, uint32 pin, std_bool level)
{
	uint32 selected = (exticr[pin / 4U] >> (4U * (pin % 4U))) & 0xFU;
	uint32 edges = regs[(level ? EXTI_RTSR : EXTI_FTSR) / 4U];

	if ((selected == port) && (((edges >> pin) & 1U) != 0U)) {
		pend(1U << pin);
	}
}

/*
 * EXTI and SYSCFG_EXTICR (see stm32_exti.h). Offsets from RM0410 11.9/7.2
 * and RM0433 20.3/12.2.
 */
#include "stm32_exti.h"
#include "cpu.h"
#include "std_device_ops.h"

#define EXTI_REGS	0xACU
#define EXTICR_SIZE	0x10U

#define LINES_MASK	0x01FFFFFFU	/* lines 0 to 24 */

static uint32 regs[EXTI_REGS / 4U];
static uint32 exticr[4];

typedef struct {
	uint32 exti_base;
	uint32 exti_size;
	uint32 syscfg_exticr1;
	uint32 imr;
	uint32 emr;
	uint32 rtsr;
	uint32 ftsr;
	uint32 swier;
	uint32 pr;
} ExtiProfileType;

static const ExtiProfileType f7_profile = {
	.exti_base = 0x40013C00U,
	.exti_size = 0x18U,
	.syscfg_exticr1 = 0x40013808U,
	.imr = 0x00U,
	.emr = 0x04U,
	.rtsr = 0x08U,
	.ftsr = 0x0CU,
	.swier = 0x10U,
	.pr = 0x14U,
};

static const ExtiProfileType h7_profile = {
	.exti_base = 0x58000000U,
	.exti_size = EXTI_REGS,
	.syscfg_exticr1 = 0x58000408U,
	.imr = 0x80U,
	.emr = 0x84U,
	.rtsr = 0x00U,
	.ftsr = 0x04U,
	.swier = 0x08U,
	.pr = 0x88U,
};

static const ExtiProfileType *profile = &f7_profile;

void stm32_exti_set_h7(std_bool h7)
{
	profile = h7 ? &h7_profile : &f7_profile;
}

static std_bool is_exticr(uint32 addr)
{
	return (addr >= profile->syscfg_exticr1) && (addr < (profile->syscfg_exticr1 + EXTICR_SIZE));
}

std_bool stm32_exti_owns(uint32 addr)
{
	return ((addr >= profile->exti_base) && (addr < (profile->exti_base + profile->exti_size))) || is_exticr(addr);
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

	regs[profile->pr / 4U] |= lines;
	for (line = 0U; line < 16U; line++) {
		if ((((lines & regs[profile->imr / 4U]) >> line) & 1U) != 0U) {
			(void)intc_raise_intr(irq_of(line));
		}
	}
}

uint32 stm32_exti_read(uint32 addr)
{
	uint32 shift = 8U * (addr & 3U);

	addr &= ~3U;
	if (is_exticr(addr)) {
		return exticr[(addr - profile->syscfg_exticr1) / 4U] >> shift;
	}
	return regs[(addr - profile->exti_base) / 4U] >> shift;
}

void stm32_exti_write(uint32 addr, uint32 size, uint32 value)
{
	uint32 shift = 8U * (addr & 3U);
	uint32 mask = ((size >= 4U) ? 0xFFFFFFFFU : ((1U << (8U * size)) - 1U)) << shift;
	uint32 offset;

	value = (value << shift) & mask;
	addr &= ~3U;
	if (is_exticr(addr)) {
		uint32 *r = &exticr[(addr - profile->syscfg_exticr1) / 4U];

		*r = ((*r & ~mask) | value) & 0xFFFFU;
		return;
	}
	offset = addr - profile->exti_base;
	if (offset == profile->pr) {
		regs[profile->pr / 4U] &= ~value;	/* rc_w1 */
	}
	else if (offset == profile->swier) {
		/* a software interrupt on each line written 1 */
		pend(value & LINES_MASK);
	}
	else {
		regs[offset / 4U] = ((regs[offset / 4U] & ~mask) | value) & LINES_MASK;
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
	uint32 edges = regs[(level ? profile->rtsr : profile->ftsr) / 4U];

	if ((selected == port) && (((edges >> pin) & 1U) != 0U)) {
		pend(1U << pin);
	}
}

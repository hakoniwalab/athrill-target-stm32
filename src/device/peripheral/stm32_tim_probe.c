/*
 * PROVISIONAL, for investigation (see stm32_tim_probe.h). Up-counting only:
 * the counter runs while CR1.CEN is set at the timer clock divided by
 * PSC + 1, wraps after ARR (SR.UIF), and sets SR.CCxIF when it reaches CCRx;
 * a flag whose DIER bit is set makes the timer's interrupt pending in the
 * NVIC (again at each tick while both are set). The counters follow the
 * clock as SysTick does (arm_m_scs_device.c).
 */
#include "stm32_tim_probe.h"
#include "cpuemu_ops.h"

#define TIM_CR1		0x00U
#define TIM_DIER	0x0CU
#define TIM_SR		0x10U
#define TIM_EGR		0x14U
#define TIM_CNT		0x24U
#define TIM_PSC		0x28U
#define TIM_ARR		0x2CU
#define TIM_CCR1	0x34U

typedef struct {
	uint32 base;
	uint32 bits;		/* 16 or 32 */
	uint32 divider;		/* CPU clocks per timer clock */
	uint32 irq_update;
	uint32 irq_compare;
	uint32 regs[0x60U / 4U];
	uint32 psc_active;	/* the prescaler in use (PSC takes effect at an update event) */
	uint64 last_clock;
	uint64 remainder;	/* CPU clocks towards the next count */
} TimType;

/* STM32F7: APB1 timers at half the CPU clock, APB2 timers (TIM1, TIM8) at the CPU clock. */
static TimType tims[] = {
	{ .base = 0x40010000U, .bits = 16U, .divider = 1U, .irq_update = 25U, .irq_compare = 27U },	/* TIM1 */
	{ .base = 0x40000000U, .bits = 32U, .divider = 2U, .irq_update = 28U, .irq_compare = 28U },	/* TIM2 */
	{ .base = 0x40000400U, .bits = 16U, .divider = 2U, .irq_update = 29U, .irq_compare = 29U },	/* TIM3 */
	{ .base = 0x40000800U, .bits = 16U, .divider = 2U, .irq_update = 30U, .irq_compare = 30U },	/* TIM4 */
	{ .base = 0x40000C00U, .bits = 32U, .divider = 2U, .irq_update = 50U, .irq_compare = 50U },	/* TIM5 */
	{ .base = 0x40010400U, .bits = 16U, .divider = 1U, .irq_update = 44U, .irq_compare = 46U },	/* TIM8 */
};

#define TIM_NUM	(sizeof(tims) / sizeof(tims[0]))

static TimType *tim_of(uint32 addr)
{
	uint32 i;

	for (i = 0U; i < TIM_NUM; i++) {
		if ((addr >= tims[i].base) && (addr < (tims[i].base + 0x60U))) {
			return &tims[i];
		}
	}
	return NULL;
}

std_bool stm32_tim_probe_owns(uint32 addr)
{
	return tim_of(addr) != NULL;
}

static uint32 reg(const TimType *t, uint32 offset)
{
	return t->regs[offset / 4U];
}

static uint32 counter_max(const TimType *t)
{
	uint32 arr = reg(t, TIM_ARR);

	return (t->bits == 16U) ? (arr & 0xFFFFU) : arr;
}

/* The counter moved by n counts: wraps, compares. */
static void count(TimType *t, uint64 n)
{
	uint64 cnt = reg(t, TIM_CNT);
	uint64 top = (uint64)counter_max(t) + 1U;
	uint64 end = cnt + n;
	uint32 k;

	for (k = 0U; k < 4U; k++) {
		uint64 ccr = reg(t, TIM_CCR1 + (4U * k));

		/* reached within (cnt, end], also after wrapping */
		if (((ccr > cnt) && (ccr <= end)) || ((end >= top) && ((ccr + top) <= end))) {
			t->regs[TIM_SR / 4U] |= 1U << (k + 1U);
		}
	}
	if (end >= top) {
		t->regs[TIM_SR / 4U] |= 1U;	/* UIF */
		t->psc_active = reg(t, TIM_PSC) & 0xFFFFU;
		end %= top;
	}
	t->regs[TIM_CNT / 4U] = (uint32)end;
}

static void advance_to(TimType *t, uint64 clock)
{
	uint64 period;
	uint64 elapsed;

	if (clock <= t->last_clock) {
		return;
	}
	elapsed = (clock - t->last_clock) + t->remainder;
	t->last_clock = clock;
	if ((reg(t, TIM_CR1) & 1U) == 0U) {
		t->remainder = 0U;
		return;
	}
	period = (uint64)t->divider * ((uint64)t->psc_active + 1U);
	count(t, elapsed / period);
	t->remainder = elapsed % period;
}

static void raise_interrupts(TimType *t)
{
	uint32 pending = reg(t, TIM_SR) & reg(t, TIM_DIER);

	if ((pending & 1U) != 0U) {
		(void)intc_raise_intr(t->irq_update);
	}
	if ((pending & 0x1EU) != 0U) {
		(void)intc_raise_intr(t->irq_compare);
	}
}

uint32 stm32_tim_probe_read(uint32 addr)
{
	TimType *t = tim_of(addr);

	advance_to(t, cpuemu_get_clock());
	return reg(t, (addr - t->base) & ~3U);
}

void stm32_tim_probe_write(uint32 addr, uint32 size, uint32 value)
{
	TimType *t = tim_of(addr);
	uint32 offset = (addr - t->base) & ~3U;
	uint32 shift = 8U * ((addr - t->base) & 3U);
	uint32 mask = ((size >= 4U) ? 0xFFFFFFFFU : ((1U << (8U * size)) - 1U)) << shift;

	advance_to(t, cpuemu_get_clock());
	value <<= shift;
	switch (offset) {
	case TIM_SR:	/* rc_w0 */
		t->regs[TIM_SR / 4U] &= value | ~mask;
		break;
	case TIM_EGR:	/* UG: the counter and the prescaler restart */
		if ((value & 1U) != 0U) {
			t->regs[TIM_CNT / 4U] = 0U;
			t->psc_active = reg(t, TIM_PSC) & 0xFFFFU;
			t->remainder = 0U;
			t->regs[TIM_SR / 4U] |= 1U;
		}
		break;
	default:
		t->regs[offset / 4U] = (t->regs[offset / 4U] & ~mask) | (value & mask);
		break;
	}
}

void stm32_tim_probe_reset(void)
{
	uint32 i;
	uint32 k;

	for (i = 0U; i < TIM_NUM; i++) {
		for (k = 0U; k < (0x60U / 4U); k++) {
			tims[i].regs[k] = 0U;
		}
		tims[i].regs[TIM_ARR / 4U] = (tims[i].bits == 16U) ? 0xFFFFU : 0xFFFFFFFFU;
		tims[i].psc_active = 0U;
		tims[i].last_clock = 0U;
		tims[i].remainder = 0U;
	}
}

/* The CPU clocks until the next flag the timer would raise with its interrupt enabled. */
static uint64 clocks_to_event(const TimType *t)
{
	uint64 cnt = reg(t, TIM_CNT);
	uint64 top = (uint64)counter_max(t) + 1U;
	uint64 counts = top - cnt;	/* to the update */
	uint64 period = (uint64)t->divider * ((uint64)t->psc_active + 1U);
	uint32 k;

	if ((reg(t, TIM_CR1) & 1U) == 0U) {
		return 0U;
	}
	for (k = 0U; k < 4U; k++) {
		uint64 ccr = reg(t, TIM_CCR1 + (4U * k));

		if (((reg(t, TIM_DIER) >> (k + 1U)) & 1U) == 0U) {
			continue;
		}
		if ((ccr > cnt) && ((ccr - cnt) < counts)) {
			counts = ccr - cnt;
		}
	}
	return (counts * period) - t->remainder;
}

void stm32_tim_probe_tick(DeviceClockType *clock)
{
	uint32 i;

	for (i = 0U; i < TIM_NUM; i++) {
		uint64 until;

		advance_to(&tims[i], clock->clock);
		raise_interrupts(&tims[i]);
		until = clocks_to_event(&tims[i]);
		if ((until != 0U) && (until < clock->min_intr_interval)) {
			clock->min_intr_interval = until;
		}
	}
}

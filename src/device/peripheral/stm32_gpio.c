/*
 * The STM32F7 GPIO ports (see stm32_gpio.h). Register offsets from RM0410
 * 6.4.
 */
#include "stm32_gpio.h"
#include "stm32_spi.h"

#define GPIO_BASE	0x40020000U
#define GPIO_STRIDE	0x400U
#define GPIO_PORTS	11U	/* A to K */

#define GPIO_MODER	0x00U
#define GPIO_OSPEEDR	0x08U
#define GPIO_PUPDR	0x0CU
#define GPIO_IDR	0x10U
#define GPIO_ODR	0x14U
#define GPIO_BSRR	0x18U
#define GPIO_REGS	0x28U

typedef struct {
	uint32 regs[GPIO_REGS / 4U];
	uint32 inputs;	/* the levels devices drive on the pins */
} GpioType;

static GpioType ports[GPIO_PORTS];

std_bool stm32_gpio_owns(uint32 addr)
{
	return (addr >= GPIO_BASE) && (addr < (GPIO_BASE + (GPIO_PORTS * GPIO_STRIDE)));
}

static GpioType *port_of(uint32 addr)
{
	return &ports[(addr - GPIO_BASE) / GPIO_STRIDE];
}

/* The output pins: MODER 01. */
static uint32 output_pins(const GpioType *g)
{
	uint32 pins = 0U;
	uint32 pin;

	for (pin = 0U; pin < 16U; pin++) {
		if (((g->regs[GPIO_MODER / 4U] >> (2U * pin)) & 3U) == 1U) {
			pins |= 1U << pin;
		}
	}
	return pins;
}

uint32 stm32_gpio_read(uint32 addr)
{
	GpioType *g = port_of(addr);
	uint32 offset = (addr - GPIO_BASE) % GPIO_STRIDE;
	uint32 shift = 8U * (offset & 3U);
	uint32 value;

	offset &= ~3U;
	if (offset == GPIO_IDR) {
		uint32 out = output_pins(g);

		value = (g->regs[GPIO_ODR / 4U] & out) | (g->inputs & ~out);
	}
	else if ((offset < GPIO_REGS) && (offset != GPIO_BSRR)) {
		value = g->regs[offset / 4U];
	}
	else {
		value = 0U;
	}
	return value >> shift;
}

/* ODR changes: the chips on the pins see their chip selects. */
static void set_odr(GpioType *g, uint32 odr)
{
	uint32 old = g->regs[GPIO_ODR / 4U];

	g->regs[GPIO_ODR / 4U] = odr & 0xFFFFU;
	if (old != g->regs[GPIO_ODR / 4U]) {
		stm32_spi_pins_changed((uint32)(g - ports), old, g->regs[GPIO_ODR / 4U]);
	}
}

void stm32_gpio_write(uint32 addr, uint32 size, uint32 value)
{
	GpioType *g = port_of(addr);
	uint32 offset = (addr - GPIO_BASE) % GPIO_STRIDE;
	uint32 shift = 8U * (offset & 3U);
	uint32 mask = ((size >= 4U) ? 0xFFFFFFFFU : ((1U << (8U * size)) - 1U)) << shift;
	uint32 odr;

	offset &= ~3U;
	value = (value << shift) & mask;
	switch (offset) {
	case GPIO_BSRR:	/* BS sets, BR resets; BS wins */
		odr = g->regs[GPIO_ODR / 4U];
		odr &= ~(value >> 16);
		odr |= value & 0xFFFFU;
		set_odr(g, odr);
		break;
	case GPIO_ODR:
		set_odr(g, (g->regs[GPIO_ODR / 4U] & ~mask) | value);
		break;
	case GPIO_IDR:
		break;
	default:
		if (offset < GPIO_REGS) {
			g->regs[offset / 4U] = (g->regs[offset / 4U] & ~mask) | value;
		}
		break;
	}
}

void stm32_gpio_reset(void)
{
	uint32 i;
	uint32 k;

	for (i = 0U; i < GPIO_PORTS; i++) {
		for (k = 0U; k < (GPIO_REGS / 4U); k++) {
			ports[i].regs[k] = 0U;
		}
		ports[i].inputs = 0U;
	}
	/* the debug pins (RM0410 6.4.1, 6.4.3, 6.4.4) */
	ports[0].regs[GPIO_MODER / 4U] = 0xA8000000U;
	ports[0].regs[GPIO_PUPDR / 4U] = 0x64000000U;
	ports[1].regs[GPIO_MODER / 4U] = 0x00000280U;
	ports[1].regs[GPIO_OSPEEDR / 4U] = 0x000000C0U;
	ports[1].regs[GPIO_PUPDR / 4U] = 0x00000100U;
}

std_bool stm32_gpio_output(uint32 port, uint32 pin)
{
	return ((ports[port].regs[GPIO_ODR / 4U] >> pin) & 1U) != 0U;
}

void stm32_gpio_drive(uint32 port, uint32 pin, std_bool level)
{
	if (level) {
		ports[port].inputs |= 1U << pin;
	}
	else {
		ports[port].inputs &= ~(1U << pin);
	}
}

#include "device.h"
#include "cpuemu_ops.h"
#include "peripheral/arm_m_scs_device.h"
#include "peripheral/arm_m_dwt.h"
#include "peripheral/stm32_periph_probe.h"
#include "peripheral/stm32_tim.h"
#include "peripheral/stm32_uart.h"
#include "peripheral/stm32_uart_tcp.h"
#include "peripheral/stm32_gpio.h"
#include "peripheral/stm32_spi.h"
#include "sensor/sensor_source.h"
#include "board/board.h"
#include "peripheral/stm32_exti.h"
#include "peripheral/stm32_dma.h"
#include "peripheral/stm32_i2c.h"
#include "peripheral/stm32_sdmmc.h"
#ifdef EXDEV_ENABLE
#include "athrill_device.h"
#endif

#ifdef EXDEV_ENABLE
static std_bool exdev_initialized = FALSE;
#endif

std_bool stm32_device_accessed = FALSE;

/* The console UART (DEVICE_CONFIG_STM32_CONSOLE_UART, USART3 by default) and its input file. */
static void uart_reset(void)
{
	uint32 console = 3U;
	uint32 tx_dma = 0U;
	char *rx_file = NULL;

	(void)cpuemu_get_devcfg_value("DEVICE_CONFIG_STM32_CONSOLE_UART", &console);
	(void)cpuemu_get_devcfg_value("DEVICE_CONFIG_STM32_UART_TX_DMA", &tx_dma);
	if (cpuemu_get_devcfg_string("DEVICE_CONFIG_STM32_UART_RX_FILE", &rx_file) != STD_E_OK) {
		rx_file = NULL;
	}
	stm32_uart_set_tx_dma(tx_dma != 0U);
	stm32_uart_reset(console, rx_file);
}

static uint32 cpu_mhz(void)
{
	uint32 mhz = 216U;

	(void)cpuemu_get_devcfg_value("DEVICE_CPU_FREQ", &mhz);
	return mhz;
}

void device_init(CpuType *cpu, DeviceClockType *clock)
{
	(void)cpu;
	clock->clock = 0U;
	clock->intclock = 0U;
	clock->min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	clock->can_skip_clock = TRUE;
	clock->is_halt = FALSE;
	arm_m_scs_reset();
	arm_m_dwt_reset();
	stm32_probe_init();
	stm32_tim_reset();
	uart_reset();
	stm32_gpio_reset();
	stm32_exti_reset();
	stm32_dma_reset();
	stm32_spi_reset();
	stm32_i2c_reset();
	stm32_sdmmc_reset((uint64)cpu_mhz() * 1000000U);
	sensor_source_reset();
	board_attach();
	stm32_uart_tcp_attach();
}

void device_supply_clock(DeviceClockType *clock)
{
	clock->can_skip_clock = TRUE;
	clock->min_intr_interval = DEVICE_CLOCK_MAX_INTERVAL;
	arm_m_scs_tick(clock);
	stm32_tim_tick(clock);
	stm32_uart_tcp_tick();
	stm32_uart_tick(clock);
	stm32_i2c_tick();
	stm32_sdmmc_tick(clock);
	board_tick(clock);
#ifdef EXDEV_ENABLE
	/* External devices (memory.txt DEV) are registered when the program is loaded. */
	if (!exdev_initialized) {
		device_init_athrill_exdev();
		exdev_initialized = TRUE;
	}
	device_supply_clock_exdev(clock);
#endif
}

void device_reset(CpuType *cpu, DeviceClockType *clock)
{
	device_init(cpu, clock);
}

void device_get_clock(DeviceClockType *clock)
{
	(void)clock;
}

/* An interrupt request of a device: interrupt intno pending in the NVIC. */
void device_raise_int(uint16 intno)
{
	(void)intc_raise_intr(intno);
}

int intc_raise_intr(uint32 intno)
{
	TargetCoreType *core = &virtual_cpu.cores[0].core;

	if (intno >= core->m.num_irq) {
		return -1;
	}
	core->m.pending[ARM_M_EXC_IRQ0 + intno] = 1U;
	core->m.pending_check = TRUE;
	return 0;
}

std_bool intc_irq_active(uint32 intno)
{
	TargetCoreType *core = &virtual_cpu.cores[0].core;

	if (intno >= core->m.num_irq) {
		return FALSE;
	}
	return core->m.active[ARM_M_EXC_IRQ0 + intno] != 0U;
}

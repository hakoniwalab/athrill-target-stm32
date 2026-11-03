/*
 * PROVISIONAL, for investigation: the STM32F7 peripheral space as a plain
 * register file, to find what a firmware (PX4/NuttX) touches at start-up.
 * Replaced by real device models as they are written.
 */
#ifndef ATHRILL_STM32_PERIPH_PROBE_H
#define ATHRILL_STM32_PERIPH_PROBE_H

#include "mpu_types.h"

#define STM32_PROBE_APB_AHB1_BASE	0x40000000U
#define STM32_PROBE_APB_AHB1_SIZE	0x00080000U
#define STM32_PROBE_AHB2_BASE		0x50000000U
#define STM32_PROBE_AHB2_SIZE		0x00070000U
/* System memory and the device's identification: UID at 0x1FF0F420, flash size at 0x1FF0F442 */
#define STM32_PROBE_SYSMEM_BASE		0x1FF00000U
#define STM32_PROBE_SYSMEM_SIZE		0x00010000U
/* DBGMCU: IDCODE at 0xE0042000 */
#define STM32_PROBE_DBGMCU_BASE		0xE0042000U
#define STM32_PROBE_DBGMCU_SIZE		0x00001000U

extern uint8 stm32_probe_apb_ahb1_data[STM32_PROBE_APB_AHB1_SIZE];
extern uint8 stm32_probe_ahb2_data[STM32_PROBE_AHB2_SIZE];
extern uint8 stm32_probe_sysmem_data[STM32_PROBE_SYSMEM_SIZE];
extern uint8 stm32_probe_dbgmcu_data[STM32_PROBE_DBGMCU_SIZE];
extern MpuAddressRegionOperationType stm32_probe_ops;

/* The identification values (UID, flash size). */
void stm32_probe_init(void);
/* The UARTs' transmit interrupts. */
void stm32_probe_tick(void);

#endif

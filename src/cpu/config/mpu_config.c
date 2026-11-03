#include "mpu.h"
#include "cpu_config.h"
#include "peripheral/arm_m_scs_device.h"
#include "peripheral/stm32_periph_probe.h"

/* Flash and SRAM come from memory.txt (ROM and RAM lines); devices are here. */
MpuAddressMapType mpu_address_map = {.dynamic_map_num = 0, .dynamic_map = NULL, .map = {
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = ARM_M_SCS_DEVICE_BASE, .size = ARM_M_SCS_DEVICE_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = arm_m_scs_data, .ops = &arm_m_scs_ops},
		/* PROVISIONAL: the rest of the peripheral space as a register file (stm32_periph_probe.c) */
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = STM32_PROBE_APB_AHB1_BASE, .size = STM32_PROBE_APB_AHB1_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = stm32_probe_apb_ahb1_data, .ops = &stm32_probe_ops},
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = STM32_PROBE_AHB2_BASE, .size = STM32_PROBE_AHB2_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = stm32_probe_ahb2_data, .ops = &stm32_probe_ops},
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = STM32_PROBE_SYSMEM_BASE, .size = STM32_PROBE_SYSMEM_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = stm32_probe_sysmem_data, .ops = &stm32_probe_ops},
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = STM32_PROBE_DBGMCU_BASE, .size = STM32_PROBE_DBGMCU_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = stm32_probe_dbgmcu_data, .ops = &stm32_probe_ops},
	}
};

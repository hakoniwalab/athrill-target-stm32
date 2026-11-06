#include "mpu.h"
#include "cpu_config.h"
#include "peripheral/arm_m_scs_device.h"
#include "peripheral/arm_m_dwt.h"
#include "peripheral/stm32_periph_probe.h"
#include "peripheral/stm32h7_boot.h"

/* Flash and SRAM come from memory.txt (ROM and RAM lines); devices are here. */
MpuAddressMapType mpu_address_map = {.dynamic_map_num = 0, .dynamic_map = NULL, .map = {
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = ARM_M_DWT_BASE, .size = ARM_M_DWT_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = arm_m_dwt_data, .ops = &arm_m_dwt_ops},
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = ARM_M_SCS_DEVICE_BASE, .size = ARM_M_SCS_DEVICE_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = arm_m_scs_data, .ops = &arm_m_scs_ops},
		/* PROVISIONAL: the rest of the peripheral space as a register file (stm32_periph_probe.c) */
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = STM32_PROBE_APB_AHB1_BASE, .size = STM32_PROBE_APB_AHB1_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = stm32_probe_apb_ahb1_data, .ops = &stm32_probe_ops},
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = STM32_PROBE_AHB2_BASE, .size = STM32_PROBE_AHB2_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = stm32_probe_ahb2_data, .ops = &stm32_probe_ops},
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = STM32_PROBE_SYSMEM_BASE, .size = STM32_PROBE_SYSMEM_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = stm32_probe_sysmem_data, .ops = &stm32_probe_ops},
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = STM32_PROBE_DBGMCU_BASE, .size = STM32_PROBE_DBGMCU_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = stm32_probe_dbgmcu_data, .ops = &stm32_probe_ops},
		/* STM32H743-only windows.  Their operations reject access in the default fmu-v5 profile. */
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = STM32H7_OTG_FS_BASE, .size = STM32H7_OTG_FS_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = stm32h7_otg_fs_data, .ops = &stm32h7_boot_ops},
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = STM32H7_AHB2_BASE, .size = STM32H7_AHB2_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = stm32h7_ahb2_data, .ops = &stm32h7_boot_ops},
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = STM32H7_AXI_BASE, .size = STM32H7_AXI_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = stm32h7_axi_data, .ops = &stm32h7_boot_ops},
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = STM32H7_D1_BASE, .size = STM32H7_D1_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = stm32h7_d1_data, .ops = &stm32h7_boot_ops},
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = STM32H7_D3_BASE, .size = STM32H7_D3_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = stm32h7_d3_data, .ops = &stm32h7_boot_ops},
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = STM32H7_DBGMCU_BASE, .size = STM32H7_DBGMCU_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = stm32h7_dbgmcu_data, .ops = &stm32h7_boot_ops},
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = STM32H7_SYSMEM_HIGH_BASE, .size = STM32H7_SYSMEM_HIGH_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = stm32h7_sysmem_high_data, .ops = &stm32h7_boot_ops},
	}
};

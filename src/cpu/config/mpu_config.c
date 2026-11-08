#include "mpu.h"
#include "cpu_config.h"
#include "peripheral/arm_m_scs_device.h"
#include "peripheral/arm_m_dwt.h"

/* Flash and SRAM come from memory.txt (ROM and RAM lines); the Cortex-M
 * system devices are here; every SoC peripheral is an athrill-device model
 * created from the Devicetree (its MMIO windows are added at run time). An
 * access to an address none of them takes fails. */
MpuAddressMapType mpu_address_map = {.dynamic_map_num = 0, .dynamic_map = NULL, .map = {
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = ARM_M_DWT_BASE, .size = ARM_M_DWT_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = arm_m_dwt_data, .ops = &arm_m_dwt_ops},
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = ARM_M_SCS_DEVICE_BASE, .size = ARM_M_SCS_DEVICE_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = arm_m_scs_data, .ops = &arm_m_scs_ops},
	}
};

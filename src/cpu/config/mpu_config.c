#include "mpu.h"
#include "cpu_config.h"
#include "peripheral/stm32_usart.h"

/* Flash and SRAM come from memory.txt (ROM and RAM lines); devices are here. */
MpuAddressMapType mpu_address_map = {.dynamic_map_num = 0, .dynamic_map = NULL, .map = {
		{.type = DEVICE, .is_malloc = FALSE, .permission = MPU_ADDRESS_REGION_PERM_ALL, .start = STM32_USART3_BASE, .size = STM32_USART_SIZE, .mask = MPU_ADDRESS_REGION_MASK_ALL, .data = stm32_usart3_data, .ops = &stm32_usart_ops},
	}
};

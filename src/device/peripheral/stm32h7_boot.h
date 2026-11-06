/*
 * STM32H743 register front end needed during reset and early board start.
 * It is deliberately separate from the STM32F7 probe: the H7 clock, power,
 * flash and system-control registers have different addresses and layouts.
 */
#ifndef ATHRILL_STM32H7_BOOT_H
#define ATHRILL_STM32H7_BOOT_H

#include "mpu_types.h"
#include "std_errno.h"

#define STM32H7_OTG_FS_BASE       0x40080000U
#define STM32H7_OTG_FS_SIZE       0x00010000U
#define STM32H7_AHB2_BASE         0x48000000U
#define STM32H7_AHB2_SIZE         0x00030000U
#define STM32H7_AXI_BASE          0x51000000U
#define STM32H7_AXI_SIZE          0x00010000U
#define STM32H7_D1_BASE           0x52000000U
#define STM32H7_D1_SIZE           0x00010000U
#define STM32H7_D3_BASE           0x58000000U
#define STM32H7_D3_SIZE           0x00028000U
#define STM32H7_DBGMCU_BASE       0x5C001000U
#define STM32H7_DBGMCU_SIZE       0x00001000U
#define STM32H7_SYSMEM_HIGH_BASE  0x1FF10000U
#define STM32H7_SYSMEM_HIGH_SIZE  0x00010000U

extern uint8 stm32h7_otg_fs_data[STM32H7_OTG_FS_SIZE];
extern uint8 stm32h7_ahb2_data[STM32H7_AHB2_SIZE];
extern uint8 stm32h7_axi_data[STM32H7_AXI_SIZE];
extern uint8 stm32h7_d1_data[STM32H7_D1_SIZE];
extern uint8 stm32h7_d3_data[STM32H7_D3_SIZE];
extern uint8 stm32h7_dbgmcu_data[STM32H7_DBGMCU_SIZE];
extern uint8 stm32h7_sysmem_high_data[STM32H7_SYSMEM_HIGH_SIZE];
extern MpuAddressRegionOperationType stm32h7_boot_ops;

void stm32h7_boot_reset(void);
Std_ReturnType stm32h7_boot_read(uint32 addr, uint32 size, uint32 *value);
Std_ReturnType stm32h7_boot_write(uint32 addr, uint32 size, uint32 value);

#endif /* ATHRILL_STM32H7_BOOT_H */

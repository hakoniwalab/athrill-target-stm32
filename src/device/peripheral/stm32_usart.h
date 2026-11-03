#ifndef ATHRILL_STM32_USART_H
#define ATHRILL_STM32_USART_H
#include "mpu_types.h"

/* STM32F7 USART3 (the Nucleo board's virtual COM port). */
#define STM32_USART3_BASE 0x40004800U
#define STM32_USART_SIZE 0x400U

extern uint8 stm32_usart3_data[STM32_USART_SIZE];
extern MpuAddressRegionOperationType stm32_usart_ops;

#endif

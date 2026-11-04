/*
 * The core's System Control Space (0xE000E000-0xE000EFFF): NVIC, SCB,
 * SysTick and the FPU's context registers, over the exception model of
 * athrill-arm-common (arm_m_scs.h).
 */
#ifndef ATHRILL_STM32_ARM_M_SCS_DEVICE_H
#define ATHRILL_STM32_ARM_M_SCS_DEVICE_H

#include "std_types.h"
#include "cpu.h"
#include "mpu_types.h"
#include "std_device_ops.h"

#define ARM_M_SCS_DEVICE_BASE	0xE000E000U
#define ARM_M_SCS_DEVICE_SIZE	0x1000U

extern uint8 arm_m_scs_data[ARM_M_SCS_DEVICE_SIZE];
extern MpuAddressRegionOperationType arm_m_scs_ops;

void arm_m_scs_reset(void);
std_bool arm_m_scs_trace_enabled(void);
/* SysTick up to the device clock; the clocks to its next interrupt into min_intr_interval. */
void arm_m_scs_tick(DeviceClockType *clock);

#endif /* ATHRILL_STM32_ARM_M_SCS_DEVICE_H */

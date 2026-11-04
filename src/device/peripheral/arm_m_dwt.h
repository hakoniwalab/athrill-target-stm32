/*
 * Cortex-M7 Data Watchpoint and Trace cycle counter.  The counter is part
 * of the Arm core, not an STM32 peripheral; only the registers needed for
 * cycle counting are modelled.
 */
#ifndef ATHRILL_STM32_ARM_M_DWT_H
#define ATHRILL_STM32_ARM_M_DWT_H

#include "std_types.h"
#include "cpu.h"
#include "mpu_types.h"
#include "std_device_ops.h"

#define ARM_M_DWT_BASE		0xE0001000U
#define ARM_M_DWT_SIZE		0x1000U

extern uint8 arm_m_dwt_data[ARM_M_DWT_SIZE];
extern MpuAddressRegionOperationType arm_m_dwt_ops;

void arm_m_dwt_reset(void);
void arm_m_dwt_trace_changed(void);
uint32 arm_m_dwt_read(uint32 addr, uint32 size);
void arm_m_dwt_write(uint32 addr, uint32 size, uint32 value);

#endif /* ATHRILL_STM32_ARM_M_DWT_H */

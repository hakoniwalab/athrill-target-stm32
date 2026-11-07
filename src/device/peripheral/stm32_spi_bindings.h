/*
 * Register interpretation of the STM32 SPI front ends from the athrill-device
 * bindings (offset, field mask or field shift by SVD register/field name),
 * resolved once at reset into plain variables used like the former #defines.
 * SPDX-License-Identifier: MIT
 */
#ifndef STM32_SPI_BINDINGS_H
#define STM32_SPI_BINDINGS_H

#include "std_types.h"

typedef enum { STM32_BIND_OFFSET, STM32_BIND_MASK, STM32_BIND_SHIFT } Stm32BindKind;

typedef struct {
	uint32 *target;
	const char *reg;
	const char *field;
	Stm32BindKind kind;
} Stm32SpiBinding;

void stm32_spi_bind(const char *compatible, const char *soc, const Stm32SpiBinding *table, uint32 count);

#endif

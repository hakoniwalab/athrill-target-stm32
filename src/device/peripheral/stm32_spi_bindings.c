/*
 * See stm32_spi_bindings.h. A missing binding stops the emulator: the
 * interpretation has a single source.
 * SPDX-License-Identifier: MIT
 */
#include "stm32_spi_bindings.h"
#include "athrill_bindings.h"

#include <stdio.h>
#include <stdlib.h>

void stm32_spi_bind(const char *compatible, const char *soc, const Stm32SpiBinding *table, uint32 count)
{
	char error[256] = "";
	const AthrillBindings *b = athrill_bindings_for(compatible, soc, error, sizeof(error));
	uint32 i;

	if (b == NULL) {
		fprintf(stderr, "SPI bindings: %s\n", error);
		exit(1);
	}
	for (i = 0U; i < count; i++) {
		uint32 offset = 0U, lsb = 0U, width = 0U;
		if (athrill_bindings_find(b, NULL, table[i].reg, table[i].field, &offset, &lsb, &width) != 0) {
			fprintf(stderr, "SPI bindings %s: no %s%s%s\n", compatible, table[i].reg,
				table[i].field != NULL ? "." : "", table[i].field != NULL ? table[i].field : "");
			exit(1);
		}
		switch (table[i].kind) {
		case STM32_BIND_OFFSET:
			*table[i].target = offset;
			break;
		case STM32_BIND_SHIFT:
			*table[i].target = lsb;
			break;
		default:
			*table[i].target = ((width >= 32U) ? 0xFFFFFFFFU : ((1U << width) - 1U)) << lsb;
			break;
		}
	}
}

/* SPDX-License-Identifier: MIT */
#ifndef ATHRILL_TARGET_STM32_TEST_CHECK_H
#define ATHRILL_TARGET_STM32_TEST_CHECK_H

#include <stdio.h>
#include <stdlib.h>

/* Unlike assert(), checks and any setup in expression remain active with NDEBUG. */
#define TEST_CHECK(expression)                                                \
	do {                                                                   \
		if (!(expression)) {                                             \
			fprintf(stderr, "%s:%d: check failed: %s\n",              \
				__FILE__, __LINE__, #expression);                     \
			exit(EXIT_FAILURE);                                        \
		}                                                              \
	} while (0)

#endif

/*
 * The DWT cycle counter checks (test_arm_m_dwt.c) on their own. They used to
 * run inside the timer test, which went with the C-path timer model (the
 * timer is an athrill-device model).
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include "std_types.h"

static uint64 now;

uint64 cpuemu_get_clock(void)
{
	return now;
}

void test_arm_m_dwt(uint64 *clock, int *checks, int *failures);

int main(void)
{
	int checks = 0;
	int failures = 0;

	test_arm_m_dwt(&now, &checks, &failures);
	printf("%d checks, %d failures\n", checks, failures);
	return (failures == 0) ? 0 : 1;
}

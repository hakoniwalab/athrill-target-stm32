/* Unit checks for the Cortex-M7 DWT cycle counter. */
#include <stdio.h>
#include "peripheral/arm_m_dwt.h"

#define DWT_CTRL	(ARM_M_DWT_BASE + 0x000U)
#define DWT_CYCCNT	(ARM_M_DWT_BASE + 0x004U)

static std_bool trace_enabled;

std_bool arm_m_scs_trace_enabled(void)
{
	return trace_enabled;
}

void test_arm_m_dwt(uint64 *now, int *checks, int *failures)
{
#define CHECK_DWT(what, got, want) do { \
	unsigned long long actual = (unsigned long long)(got); \
	unsigned long long expected = (unsigned long long)(want); \
	(*checks)++; \
	if (actual != expected) { \
		(*failures)++; \
		printf("FAIL DWT: %s = 0x%llx, want 0x%llx\n", what, actual, expected); \
	} \
} while (0)

	*now = 0U;
	trace_enabled = FALSE;
	arm_m_dwt_reset();
	CHECK_DWT("four comparators and counter disabled", arm_m_dwt_read(DWT_CTRL, 4U), 4U << 28);
	arm_m_dwt_write(DWT_CTRL, 4U, 1U);
	*now = 100U;
	CHECK_DWT("TRCENA gates CYCCNT", arm_m_dwt_read(DWT_CYCCNT, 4U), 0U);
	arm_m_dwt_trace_changed();
	trace_enabled = TRUE;
	*now = 125U;
	CHECK_DWT("CYCCNT follows virtual CPU clocks", arm_m_dwt_read(DWT_CYCCNT, 4U), 25U);
	arm_m_dwt_write(DWT_CYCCNT, 4U, 0xFFFFFFF0U);
	*now = 157U;
	CHECK_DWT("CYCCNT wraps at 32 bits", arm_m_dwt_read(DWT_CYCCNT, 4U), 0x10U);
	arm_m_dwt_write(DWT_CTRL, 1U, 0U);
	*now = 200U;
	CHECK_DWT("clearing CYCCNTENA stops the counter", arm_m_dwt_read(DWT_CYCCNT, 4U), 0x10U);
	arm_m_dwt_write(DWT_CYCCNT + 1U, 1U, 0xABU);
	CHECK_DWT("byte write to CYCCNT", arm_m_dwt_read(DWT_CYCCNT, 4U), 0xAB10U);

#undef CHECK_DWT
}

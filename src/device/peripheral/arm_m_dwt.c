/*
 * Cortex-M7 DWT registers used by firmware for cycle counting.  CYCCNT is
 * advanced from Athrill's virtual CPU clock when both DEMCR.TRCENA and
 * DWT_CTRL.CYCCNTENA are set.  No per-clock work is needed.
 */
#include "arm_m_dwt.h"
#include "arm_m_scs_device.h"
#include "cpuemu_ops.h"
#include "std_errno.h"

#define DWT_CTRL_OFFSET		0x000U
#define DWT_CYCCNT_OFFSET	0x004U
#define DWT_CTRL_CYCCNTENA	(1U << 0)
#define DWT_CTRL_NUMCOMP	(4U << 28)

uint8 arm_m_dwt_data[ARM_M_DWT_SIZE];

static uint32 control;
static uint32 cycle_count;
static uint64 last_clock;

static uint32 access_mask(uint32 addr, uint32 size)
{
	uint32 bytes;

	bytes = (size >= 4U) ? 0xFFFFFFFFU : ((1U << (8U * size)) - 1U);
	return bytes << (8U * (addr & 3U));
}

static void advance_to(uint64 clock)
{
	if (clock <= last_clock) {
		return;
	}
	if (arm_m_scs_trace_enabled() && ((control & DWT_CTRL_CYCCNTENA) != 0U)) {
		cycle_count += (uint32)(clock - last_clock);
	}
	last_clock = clock;
}

void arm_m_dwt_reset(void)
{
	control = 0U;
	cycle_count = 0U;
	last_clock = 0U;
}

void arm_m_dwt_trace_changed(void)
{
	advance_to(cpuemu_get_clock());
}

uint32 arm_m_dwt_read(uint32 addr, uint32 size)
{
	uint32 offset = addr - ARM_M_DWT_BASE;
	uint32 value;

	advance_to(cpuemu_get_clock());
	switch (offset & ~3U) {
	case DWT_CTRL_OFFSET:
		value = DWT_CTRL_NUMCOMP | control;
		break;
	case DWT_CYCCNT_OFFSET:
		value = cycle_count;
		break;
	default:
		value = 0U;
		break;
	}
	return (value & access_mask(addr, size)) >> (8U * (addr & 3U));
}

void arm_m_dwt_write(uint32 addr, uint32 size, uint32 value)
{
	uint32 offset = addr - ARM_M_DWT_BASE;
	uint32 mask = access_mask(addr, size);
	uint32 shifted = value << (8U * (addr & 3U));

	advance_to(cpuemu_get_clock());
	switch (offset & ~3U) {
	case DWT_CTRL_OFFSET:
		control = ((control & ~mask) | (shifted & mask)) & DWT_CTRL_CYCCNTENA;
		break;
	case DWT_CYCCNT_OFFSET:
		cycle_count = (cycle_count & ~mask) | (shifted & mask);
		break;
	default:
		break;
	}
}

static Std_ReturnType get8(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 *d)
{
	(void)r;
	(void)c;
	*d = (uint8)arm_m_dwt_read((uint32)a, 1U);
	return STD_E_OK;
}

static Std_ReturnType get16(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint16 *d)
{
	(void)r;
	(void)c;
	*d = (uint16)arm_m_dwt_read((uint32)a, 2U);
	return STD_E_OK;
}

static Std_ReturnType get32(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint32 *d)
{
	(void)r;
	(void)c;
	*d = arm_m_dwt_read((uint32)a, 4U);
	return STD_E_OK;
}

static Std_ReturnType put8(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 d)
{
	(void)r;
	(void)c;
	arm_m_dwt_write((uint32)a, 1U, d);
	return STD_E_OK;
}

static Std_ReturnType put16(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint16 d)
{
	(void)r;
	(void)c;
	arm_m_dwt_write((uint32)a, 2U, d);
	return STD_E_OK;
}

static Std_ReturnType put32(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint32 d)
{
	(void)r;
	(void)c;
	arm_m_dwt_write((uint32)a, 4U, d);
	return STD_E_OK;
}

static Std_ReturnType get_pointer(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 **d)
{
	(void)c;
	*d = &r->data[a - r->start];
	return STD_E_OK;
}

MpuAddressRegionOperationType arm_m_dwt_ops = { get8, get16, get32, put8, put16, put32, get_pointer };

/*
 * The memory windows (see memory_window.h).
 */
#include "memory_window.h"
#include "mpu.h"

MemoryWindowType memory_windows[2];

static std_bool window_holds(const MemoryWindowType *w, uint32 addr, uint32 size)
{
	return ((addr - w->start) < w->size) && ((w->size - (addr - w->start)) >= size);
}

uint8 *memory_window_lookup(uint32 addr, uint32 size)
{
	MemoryWindowType found;
	uint32 i;

	if (window_holds(&memory_windows[1], addr, size)) {
		found = memory_windows[1];
		memory_windows[1] = memory_windows[0];
		memory_windows[0] = found;
		return &found.data[addr - found.start];
	}
	for (i = 0U; i < mpu_address_map.dynamic_map_num; i++) {
		const MpuAddressRegionType *r = &mpu_address_map.dynamic_map[i];

		/* plain memory, without address aliasing */
		if ((r->ops != &default_memory_operation) || (r->data == NULL)
			|| (r->mask != MPU_ADDRESS_REGION_MASK_ALL)) {
			continue;
		}
		found.start = (uint32)r->start;
		found.size = (uint32)r->size;
		found.data = r->data;
		if (window_holds(&found, addr, size)) {
			memory_windows[1] = memory_windows[0];
			memory_windows[0] = found;
			return &found.data[addr - found.start];
		}
	}
	return NULL;
}

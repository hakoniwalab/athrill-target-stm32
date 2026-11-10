/*
 * The memory windows (see memory_window.h).
 */
#include "memory_window.h"
#include "mpu.h"

MemoryWindowType memory_windows[2];
MemoryWindowType memory_code_window;

static std_bool window_holds(const MemoryWindowType *w, uint32 addr, uint32 size)
{
	return ((addr - w->start) < w->size) && ((w->size - (addr - w->start)) >= size);
}

/* The plain memory region holding [addr, addr + size) into *found; FALSE: none. */
static std_bool find_region(uint32 addr, uint32 size, MemoryWindowType *found)
{
	uint32 i;

	for (i = 0U; i < mpu_address_map.dynamic_map_num; i++) {
		const MpuAddressRegionType *r = &mpu_address_map.dynamic_map[i];

		/* plain memory, without address aliasing */
		if ((r->ops != &default_memory_operation) || (r->data == NULL)
			|| (r->mask != MPU_ADDRESS_REGION_MASK_ALL)) {
			continue;
		}
		found->start = (uint32)r->start;
		found->size = (uint32)r->size;
		found->data = r->data;
		found->writable = (r->type != READONLY_MEMORY);
		if (window_holds(found, addr, size)) {
			return TRUE;
		}
	}
	return FALSE;
}

std_bool memory_window_region(uint32 addr, MemoryWindowType *region)
{
	return find_region(addr, 1U, region);
}

uint8 *memory_window_lookup(uint32 addr, uint32 size)
{
	MemoryWindowType found;

	if (window_holds(&memory_windows[1], addr, size)) {
		found = memory_windows[1];
	}
	else if (!find_region(addr, size, &found)) {
		return NULL;
	}
	memory_windows[1] = memory_windows[0];
	memory_windows[0] = found;
	return &found.data[addr - found.start];
}

uint8 *memory_window_code_lookup(uint32 addr, uint32 size)
{
	MemoryWindowType found;

	if (!find_region(addr, size, &found)) {
		return NULL;
	}
	memory_code_window = found;
	return &found.data[addr - found.start];
}

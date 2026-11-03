/*
 * Direct access to plain memory (the ROM and RAM regions of memory.txt) for
 * the hot paths: instruction fetch and data loads and stores. The last two
 * memory regions used are remembered (code in flash and data in RAM
 * alternate), and an access inside one goes to the host memory without the
 * bus and the MPU's region search. Device registers and anything else take
 * the bus as before.
 */
#ifndef ATHRILL_STM32_MEMORY_WINDOW_H
#define ATHRILL_STM32_MEMORY_WINDOW_H

#include "std_types.h"

typedef struct {
	uint32 start;
	uint32 size;		/* 0: empty */
	uint8 *data;
} MemoryWindowType;

extern MemoryWindowType memory_windows[2];

/* The memory region holding [addr, addr + size) in a window, or NULL; it becomes window 0. */
uint8 *memory_window_lookup(uint32 addr, uint32 size);

/* The host address of [addr, addr + size) if it is plain memory, or NULL. */
static inline uint8 *memory_window_pointer(uint32 addr, uint32 size)
{
	const MemoryWindowType *w = &memory_windows[0];

	if (((addr - w->start) < w->size) && ((w->size - (addr - w->start)) >= size)) {
		return &w->data[addr - w->start];
	}
	return memory_window_lookup(addr, size);
}

#endif /* ATHRILL_STM32_MEMORY_WINDOW_H */

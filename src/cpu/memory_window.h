/*
 * Direct access to plain memory (the ROM and RAM regions of memory.txt) for
 * the hot paths: instruction fetch and data loads and stores. Instruction
 * fetch remembers its own region (the code window), data accesses the last
 * two they used (data in RAM, constants in flash), so neither evicts the
 * other's; an access inside one goes to the host memory without the bus and
 * the MPU's region search. Device registers and anything else take the bus
 * as before.
 */
#ifndef ATHRILL_STM32_MEMORY_WINDOW_H
#define ATHRILL_STM32_MEMORY_WINDOW_H

#include "std_types.h"

typedef struct {
	uint32 start;
	uint32 size;		/* 0: empty */
	uint8 *data;
	std_bool writable;	/* RAM; a ROM region is read only, as the bus has it */
} MemoryWindowType;

extern MemoryWindowType memory_windows[2];
extern MemoryWindowType memory_code_window;

/* The memory region holding [addr, addr + size) in a window, or NULL; it becomes window 0. */
uint8 *memory_window_lookup(uint32 addr, uint32 size);
/* The same for instruction fetch: the region becomes the code window. */
uint8 *memory_window_code_lookup(uint32 addr, uint32 size);

/* The host address of instruction bytes [addr, addr + size) if they are plain memory, or NULL. */
static inline uint8 *memory_window_code_pointer(uint32 addr, uint32 size)
{
	const MemoryWindowType *w = &memory_code_window;

	if (((addr - w->start) < w->size) && ((w->size - (addr - w->start)) >= size)) {
		return &w->data[addr - w->start];
	}
	return memory_window_code_lookup(addr, size);
}

/* The host address of [addr, addr + size) if it is plain memory, or NULL. */
static inline uint8 *memory_window_pointer(uint32 addr, uint32 size)
{
	const MemoryWindowType *w = &memory_windows[0];

	if (((addr - w->start) < w->size) && ((w->size - (addr - w->start)) >= size)) {
		return &w->data[addr - w->start];
	}
	return memory_window_lookup(addr, size);
}

/* The same for a write: NULL also for ROM (the bus then refuses the write). */
static inline uint8 *memory_window_write_pointer(uint32 addr, uint32 size)
{
	const MemoryWindowType *w = &memory_windows[0];

	if (w->writable && ((addr - w->start) < w->size) && ((w->size - (addr - w->start)) >= size)) {
		return &w->data[addr - w->start];
	}
	if (memory_window_lookup(addr, size) == NULL) {
		return NULL;
	}
	return memory_windows[0].writable ? &memory_windows[0].data[addr - memory_windows[0].start] : NULL;
}

/* Whether the code window (the last memory_window_code_pointer) is ROM: its instructions cannot change. */
static inline std_bool memory_window_code_is_rom(void)
{
	return !memory_code_window.writable;
}

#endif /* ATHRILL_STM32_MEMORY_WINDOW_H */

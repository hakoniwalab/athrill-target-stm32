#include "concrete_executor/target/dbg_target_cpu.h"
#include "cpu.h"
#include <stdio.h>

void dbg_target_print_cpu(uint32 id)
{
	unsigned i;
	TargetCoreType *c = &virtual_cpu.cores[id].core;

	printf("***CPU<%u> ARMv7-M***\nPC 0x%08x APSR 0x%08x\n", id, c->reg.pc, c->reg.apsr);
	for (i = 0; i < 15; i++) {
		printf("r%-2u 0x%08x%c", i, c->reg.r[i], (i % 4) == 3 ? '\n' : ' ');
	}
	printf("\n");
}

void cpu_debug_print_mpu_status(uint32 id)
{
	(void)id;
}

#ifdef ATHRILL_ENABLE_GDB_RSP
#include "debugger/rsp/gdb_rsp_target.h"
#include <string.h>

/*
 * GDB RSP register image (hakoniwalab/athrill-target-stm32#24): the layout
 * gdb uses for an M-profile ELF without a target description, r0-r12, sp,
 * lr, pc (gdb's 0-15), then xPSR (gdb's 25, the 17th register of the g
 * packet); 32 bits each, little endian.
 *
 * xPSR = APSR | IPSR | EPSR: EPSR's T bit is always set (Thumb only) and its
 * IT bits are ITSTATE (IT[1:0] at bits 26:25, IT[7:2] at bits 15:10). A
 * write sets APSR's flags (N Z C V Q, bits 31:27, and GE, bits 19:16); IPSR
 * and EPSR are not writable from the debugger.
 */
#define GDB_REG_PC 15U
#define GDB_REG_XPSR 16U
#define GDB_REG_COUNT 17U
#define APSR_WRITABLE 0xF80F0000U

uint32 dbg_target_gdb_reg_count(void)
{
	return GDB_REG_COUNT;
}

uint32 dbg_target_gdb_reg_size(uint32 regno)
{
	(void)regno;
	return 4U;
}

static uint32 xpsr_of(const TargetCoreType *c)
{
	uint32 epsr = (1U << 24) | ((c->reg.itstate & 0x3U) << 25) | ((c->reg.itstate >> 2) << 10);
	return (c->reg.apsr & APSR_WRITABLE) | (c->m.ipsr & 0x1FFU) | epsr;
}

std_bool dbg_target_gdb_read_reg(uint32 core_id, uint32 regno, uint8 *bytes)
{
	const TargetCoreType *c = &virtual_cpu.cores[core_id].core;
	uint32 value;

	if (regno < GDB_REG_PC) {
		value = c->reg.r[regno];
	}
	else if (regno == GDB_REG_PC) {
		value = c->reg.pc;
	}
	else if (regno == GDB_REG_XPSR) {
		value = xpsr_of(c);
	}
	else {
		return FALSE;
	}
	bytes[0] = (uint8)value;
	bytes[1] = (uint8)(value >> 8);
	bytes[2] = (uint8)(value >> 16);
	bytes[3] = (uint8)(value >> 24);
	return TRUE;
}

std_bool dbg_target_gdb_write_reg(uint32 core_id, uint32 regno, const uint8 *bytes)
{
	TargetCoreType *c = &virtual_cpu.cores[core_id].core;
	uint32 value = (uint32)bytes[0] | ((uint32)bytes[1] << 8) | ((uint32)bytes[2] << 16) |
		((uint32)bytes[3] << 24);

	if (regno < GDB_REG_PC) {
		c->reg.r[regno] = value;
	}
	else if (regno == GDB_REG_PC) {
		c->reg.pc = value & ~1U;
	}
	else if (regno == GDB_REG_XPSR) {
		c->reg.apsr = (c->reg.apsr & ~APSR_WRITABLE) | (value & APSR_WRITABLE);
	}
	else {
		return FALSE;
	}
	return TRUE;
}

void dbg_target_gdb_set_pc(uint32 core_id, uint32 pc)
{
	virtual_cpu.cores[core_id].core.reg.pc = pc & ~1U;
}
#endif /* ATHRILL_ENABLE_GDB_RSP */

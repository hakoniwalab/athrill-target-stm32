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
#include "arm_common/arm_m_exception.h"
#include "arm_common/arm_pseudo_m_system.h"
#include <string.h>

/*
 * GDB RSP register image (hakoniwalab/athrill-target-stm32#24), little
 * endian, in the order of the target description below:
 *    0-15  r0-r12, sp (the current stack pointer), lr, pc
 *      16  xPSR = APSR | IPSR | EPSR (the Thumb bit; IT[1:0] at bits 26:25,
 *          IT[7:2] at bits 15:10 from ITSTATE); a write sets only the APSR
 *          flags (N Z C V Q, bits 31:27, and GE, bits 19:16)
 *   17-18  MSP, PSP (one of them is the current sp)
 *   19-22  PRIMASK, BASEPRI, FAULTMASK, CONTROL (CONTROL read only)
 *   23-38  d0-d15 (64 bits each, d[n] = s[2n+1]:s[2n]); GDB shows s0-s31
 *      39  FPSCR
 * Without a target description (a core before toppers/athrill#85) GDB
 * reads only the first 17, the default M-profile layout.
 */
#define GDB_REG_SP 13U
#define GDB_REG_PC 15U
#define GDB_REG_XPSR 16U
#define GDB_REG_MSP 17U
#define GDB_REG_PSP 18U
#define GDB_REG_PRIMASK 19U
#define GDB_REG_BASEPRI 20U
#define GDB_REG_FAULTMASK 21U
#define GDB_REG_CONTROL 22U
#define GDB_REG_D0 23U
#define GDB_REG_FPSCR 39U
#ifdef ATHRILL_GDB_RSP_TARGET_XML
#define GDB_REG_COUNT 40U
#else
#define GDB_REG_COUNT 17U	/* no target description: GDB's default M-profile layout */
#endif
#define APSR_WRITABLE 0xF80F0000U

#ifdef ATHRILL_GDB_RSP_TARGET_XML
static const char target_xml[] =
	"<?xml version=\"1.0\"?>"
	"<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
	"<target version=\"1.0\">"
	"<architecture>arm</architecture>"
	"<feature name=\"org.gnu.gdb.arm.m-profile\">"
	"<reg name=\"r0\" bitsize=\"32\" regnum=\"0\"/>"
	"<reg name=\"r1\" bitsize=\"32\"/><reg name=\"r2\" bitsize=\"32\"/><reg name=\"r3\" bitsize=\"32\"/>"
	"<reg name=\"r4\" bitsize=\"32\"/><reg name=\"r5\" bitsize=\"32\"/><reg name=\"r6\" bitsize=\"32\"/>"
	"<reg name=\"r7\" bitsize=\"32\"/><reg name=\"r8\" bitsize=\"32\"/><reg name=\"r9\" bitsize=\"32\"/>"
	"<reg name=\"r10\" bitsize=\"32\"/><reg name=\"r11\" bitsize=\"32\"/><reg name=\"r12\" bitsize=\"32\"/>"
	"<reg name=\"sp\" bitsize=\"32\" type=\"data_ptr\"/>"
	"<reg name=\"lr\" bitsize=\"32\"/>"
	"<reg name=\"pc\" bitsize=\"32\" type=\"code_ptr\"/>"
	"<reg name=\"xpsr\" bitsize=\"32\" regnum=\"16\"/>"
	"</feature>"
	"<feature name=\"org.gnu.gdb.arm.m-system\">"
	"<reg name=\"msp\" bitsize=\"32\" regnum=\"17\" type=\"data_ptr\" group=\"system\"/>"
	"<reg name=\"psp\" bitsize=\"32\" regnum=\"18\" type=\"data_ptr\" group=\"system\"/>"
	"</feature>"
	"<feature name=\"org.hakoniwalab.athrill.arm.m-special\">"
	"<reg name=\"primask\" bitsize=\"32\" regnum=\"19\" group=\"system\"/>"
	"<reg name=\"basepri\" bitsize=\"32\" regnum=\"20\" group=\"system\"/>"
	"<reg name=\"faultmask\" bitsize=\"32\" regnum=\"21\" group=\"system\"/>"
	"<reg name=\"control\" bitsize=\"32\" regnum=\"22\" group=\"system\"/>"
	"</feature>"
	"<feature name=\"org.gnu.gdb.arm.vfp\">"
	"<reg name=\"d0\" bitsize=\"64\" regnum=\"23\" type=\"ieee_double\" group=\"float\"/>"
	"<reg name=\"d1\" bitsize=\"64\" type=\"ieee_double\" group=\"float\"/>"
	"<reg name=\"d2\" bitsize=\"64\" type=\"ieee_double\" group=\"float\"/>"
	"<reg name=\"d3\" bitsize=\"64\" type=\"ieee_double\" group=\"float\"/>"
	"<reg name=\"d4\" bitsize=\"64\" type=\"ieee_double\" group=\"float\"/>"
	"<reg name=\"d5\" bitsize=\"64\" type=\"ieee_double\" group=\"float\"/>"
	"<reg name=\"d6\" bitsize=\"64\" type=\"ieee_double\" group=\"float\"/>"
	"<reg name=\"d7\" bitsize=\"64\" type=\"ieee_double\" group=\"float\"/>"
	"<reg name=\"d8\" bitsize=\"64\" type=\"ieee_double\" group=\"float\"/>"
	"<reg name=\"d9\" bitsize=\"64\" type=\"ieee_double\" group=\"float\"/>"
	"<reg name=\"d10\" bitsize=\"64\" type=\"ieee_double\" group=\"float\"/>"
	"<reg name=\"d11\" bitsize=\"64\" type=\"ieee_double\" group=\"float\"/>"
	"<reg name=\"d12\" bitsize=\"64\" type=\"ieee_double\" group=\"float\"/>"
	"<reg name=\"d13\" bitsize=\"64\" type=\"ieee_double\" group=\"float\"/>"
	"<reg name=\"d14\" bitsize=\"64\" type=\"ieee_double\" group=\"float\"/>"
	"<reg name=\"d15\" bitsize=\"64\" type=\"ieee_double\" group=\"float\"/>"
	"<reg name=\"fpscr\" bitsize=\"32\" regnum=\"39\" type=\"int\" group=\"float\"/>"
	"</feature>"
	"</target>";
#endif

#ifdef ATHRILL_GDB_RSP_TARGET_XML
const char *dbg_target_gdb_target_xml(void)
{
	return target_xml;
}
#endif

uint32 dbg_target_gdb_reg_count(void)
{
	return GDB_REG_COUNT;
}

uint32 dbg_target_gdb_reg_size(uint32 regno)
{
	return (regno >= GDB_REG_D0 && regno < GDB_REG_FPSCR) ? 8U : 4U;
}

static uint32 xpsr_of(const TargetCoreType *c)
{
	uint32 epsr = (1U << 24) | ((c->reg.itstate & 0x3U) << 25) | ((c->reg.itstate >> 2) << 10);
	return (c->reg.apsr & APSR_WRITABLE) | (c->m.ipsr & 0x1FFU) | epsr;
}

static void put32(uint8 *bytes, uint32 value)
{
	bytes[0] = (uint8)value;
	bytes[1] = (uint8)(value >> 8);
	bytes[2] = (uint8)(value >> 16);
	bytes[3] = (uint8)(value >> 24);
}

static uint32 get32(const uint8 *bytes)
{
	return (uint32)bytes[0] | ((uint32)bytes[1] << 8) | ((uint32)bytes[2] << 16) |
		((uint32)bytes[3] << 24);
}

std_bool dbg_target_gdb_read_reg(uint32 core_id, uint32 regno, uint8 *bytes)
{
	const TargetCoreType *c = &virtual_cpu.cores[core_id].core;

	if (regno < GDB_REG_PC) {
		put32(bytes, c->reg.r[regno]);
	}
	else if (regno == GDB_REG_PC) {
		put32(bytes, c->reg.pc);
	}
	else if (regno == GDB_REG_XPSR) {
		put32(bytes, xpsr_of(c));
	}
	else if (regno == GDB_REG_MSP) {
		put32(bytes, ArmMSPMain(c));
	}
	else if (regno == GDB_REG_PSP) {
		put32(bytes, ArmMSPProcess(c));
	}
	else if (regno == GDB_REG_PRIMASK) {
		put32(bytes, c->m.primask);
	}
	else if (regno == GDB_REG_BASEPRI) {
		put32(bytes, c->m.basepri);
	}
	else if (regno == GDB_REG_FAULTMASK) {
		put32(bytes, c->m.faultmask);
	}
	else if (regno == GDB_REG_CONTROL) {
		put32(bytes, c->m.control);
	}
	else if (regno < GDB_REG_FPSCR) {
		uint32 n = regno - GDB_REG_D0;
		put32(bytes, c->reg.s[2U * n]);
		put32(bytes + 4, c->reg.s[2U * n + 1U]);
	}
	else if (regno == GDB_REG_FPSCR) {
		put32(bytes, c->reg.fpscr);
	}
	else {
		return FALSE;
	}
	return TRUE;
}

std_bool dbg_target_gdb_write_reg(uint32 core_id, uint32 regno, const uint8 *bytes)
{
	TargetCoreType *c = &virtual_cpu.cores[core_id].core;
	uint32 value = get32(bytes);

	if (regno < GDB_REG_PC) {
		c->reg.r[regno] = value;
	}
	else if (regno == GDB_REG_PC) {
		c->reg.pc = value & ~1U;
	}
	else if (regno == GDB_REG_XPSR) {
		c->reg.apsr = (c->reg.apsr & ~APSR_WRITABLE) | (value & APSR_WRITABLE);
	}
	else if (regno == GDB_REG_MSP) {
		ArmMSPMainWrite(c, value & ~3U);
	}
	else if (regno == GDB_REG_PSP) {
		ArmMSPProcessWrite(c, value & ~3U);
	}
	else if (regno == GDB_REG_PRIMASK || regno == GDB_REG_BASEPRI || regno == GDB_REG_FAULTMASK) {
		/* as MSR does: the execution priority may change */
		if (regno == GDB_REG_PRIMASK) {
			c->m.primask = value & 1U;
		}
		else if (regno == GDB_REG_BASEPRI) {
			c->m.basepri = value & ArmMPriorityMask(&c->m);
		}
		else {
			c->m.faultmask = value & 1U;
		}
		c->m.priority_check = TRUE;
	}
	else if (regno == GDB_REG_CONTROL) {
		return FALSE;	/* SPSEL and nPRIV change the mode's stacks: not from the debugger */
	}
	else if (regno < GDB_REG_FPSCR) {
		uint32 n = regno - GDB_REG_D0;
		c->reg.s[2U * n] = value;
		c->reg.s[2U * n + 1U] = get32(bytes + 4);
	}
	else if (regno == GDB_REG_FPSCR) {
		c->reg.fpscr = value;
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

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

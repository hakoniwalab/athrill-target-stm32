#include "athrill_idle_spin_halt.h"
#include "cpu.h"
#include "thumb_exec.h"
#include "arm_common/arm_m_exception.h"

static std_bool arm_halt_ready(CoreIdType core_id)
{
	TargetCoreType *core = &virtual_cpu.cores[core_id].core;

	return (core->reg.itstate == 0U) && (ArmMPendingToTakeFast(&core->m) == 0U);
}

static const AthrillIdleSpinHaltTargetOps arm_idle_spin_halt_ops = {
	.name = "Armv7-M",
	.is_branch_to_self = thumb_is_branch_to_self,
	.halt_ready = arm_halt_ready,
};

void stm32_idle_spin_halt_init(void)
{
	athrill_idle_spin_halt_register_target(&arm_idle_spin_halt_ops);
}

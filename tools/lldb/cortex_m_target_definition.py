# SPDX-License-Identifier: MIT
# Copyright 2026 Hakoniwa Laboratory
"""
LLDB target definition for Athrill STM32's GDB server (athrill3 -g<port>).

The server sends the register image gdb uses for an M-profile ELF: r0-r12,
sp, lr, pc, xpsr, 32 bits each (src/debugger/executor/concrete_executor/
dbg_target_cpu.c). LLDB asks the server for that layout (qRegisterInfo or a
target description), which Athrill's server does not answer, so this file
tells LLDB instead:

    lldb px4_fmu-v5_default.elf
    (lldb) settings set plugin.process.gdb-remote.target-definition-file tools/lldb/cortex_m_target_definition.py
    (lldb) gdb-remote 1234
"""

import lldb

# DWARF register numbers of AAPCS (r0-r15); xPSR has none.
_NAMES = ["r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12", "sp", "lr", "pc", "xpsr"]
_ALT = {"r7": "fp", "r13": "sp", "r14": "lr", "r15": "pc"}
_GENERIC = {"sp": lldb.LLDB_REGNUM_GENERIC_SP, "lr": lldb.LLDB_REGNUM_GENERIC_RA,
            "pc": lldb.LLDB_REGNUM_GENERIC_PC, "r7": lldb.LLDB_REGNUM_GENERIC_FP,
            "xpsr": lldb.LLDB_REGNUM_GENERIC_FLAGS}


def get_target_definition():
    registers = []
    for index, name in enumerate(_NAMES):
        register = {
            "name": name,
            "set": 0,
            "bitsize": 32,
            "offset": index * 4,
            "encoding": lldb.eEncodingUint,
            "format": lldb.eFormatHex,
        }
        if index < 16:
            register["dwarf"] = index
            register["gcc"] = index
        else:
            register["dwarf"] = lldb.LLDB_INVALID_REGNUM
            register["gcc"] = lldb.LLDB_INVALID_REGNUM
        if name in _GENERIC:
            register["generic"] = _GENERIC[name]
        if index in (13, 14, 15):
            register["alt-name"] = "r%d" % index
        registers.append(register)
    return {
        "sets": ["General Purpose Registers"],
        "registers": registers,
        "host-info": {"triple": "arm-none-eabihf", "endian": "little", "ptrsize": 4},
    }


def get_dynamic_setting(target, setting_name):
    """LLDB calls this for plugin.process.gdb-remote.target-definition-file."""
    if setting_name == "gdb-server-target-definition":
        return get_target_definition()
    return None

#!/usr/bin/env python3
"""Cross-check the generated 16-bit Thumb decoder against GNU objdump and
llvm-objdump (docs/design.md, decision 10).

Every halfword that is not the first half of a 32-bit instruction is decoded
three ways: by the generated table (src/cpu/decoder/t32_decode16.c) and by the
two disassemblers. The instruction names of Arm's data (for example MOV_r_T2)
are mapped to the mnemonics the disassemblers print (lsls, lsrs, asrs, movs);
a halfword passes when its decoded name maps to the mnemonic a disassembler
printed, or when both call it undefined.

Usage (listings made in the toolchain image, see the README):
  python tools/crosscheck_t32_decoder.py <gnu listing> <llvm listing>
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABLE = os.path.join(ROOT, "src", "cpu", "decoder", "t32_decode16.c")

CONDITIONS = ("eq", "ne", "cs", "hs", "cc", "lo", "mi", "pl", "vs", "vc",
              "hi", "ls", "ge", "lt", "gt", "le", "al")

# Data names (before the encoding suffix) whose mnemonics differ from the name.
ALIASES = {
    "MOV_r_T2": {"lsl", "lsr", "asr", "mov"},
    "MOV_rr_T1_LSL": {"lsl"}, "MOV_rr_T1_LSR": {"lsr"},
    "MOV_rr_T1_ASR": {"asr"}, "MOV_rr_T1_ROR": {"ror"},
    "ADR": {"add", "adr"},
    "MOV_r_T1": {"mov", "nop"},	# mov r8, r8 is printed as nop
    "RSB": {"rsb", "neg"},
    "STM": {"stm", "stmia", "stmea"},
    "LDM": {"ldm", "ldmia", "ldmfd", "pop"},
    "STMDB": {"push", "stmdb"},
    "PUSH": {"push"}, "POP": {"pop"},
    "CPS": {"cpsie", "cpsid"},
    "IT": {"it", "itt", "ite", "ittt", "itte", "itet", "itee", "itttt", "ittte", "ittet",
           "ittee", "itett", "itete", "iteet", "iteee"},
    "NOP": {"nop", "hint"},	# llvm-objdump prints unallocated hints as hint #n
    "UDF": {"udf", "trap", "__brkdiv0"},	# llvm-objdump prints two UDF values by their use
}


def load_table():
    text = open(TABLE).read()
    names = re.findall(r'^\t"([^"]+)",$', text.split("t32_decode16_names")[1].split("};")[0], re.M)
    level1 = [int(v) for v in re.findall(r"\d+", text.split("t32_decode16_level1[256] = {")[1].split("};")[0])]
    blocks_text = text.split("t32_decode16_blocks")[1].split("= {", 1)[1].rsplit("};", 1)[0]
    blocks = [[int(v) for v in re.findall(r"\d+", b)] for b in blocks_text.split("},") if re.search(r"\d", b)]
    return names, lambda insn: names[blocks[level1[insn >> 8]][insn & 0xFF]]


def candidates(mnemonic):
    """The mnemonic in lower case without .n/.w, and the same without a
    condition suffix: disassemblers carry an IT instruction's condition over
    to the following halfwords of the listing. Both are tried, since a suffix
    like "vs" may also end a mnemonic (movs)."""
    m = mnemonic.lower().split(".")[0].split("<")[0]
    names = {m}
    for c in CONDITIONS:
        if m.endswith(c) and len(m) > len(c):
            names.add(m[:-len(c)])
    return names


def accepted(name):
    """Mnemonics for the data's instruction name, with and without 's'."""
    base = name.split("_")[0]
    names = ALIASES.get(name) or ALIASES.get(base) or {base.lower()}
    return set(names) | {n + "s" for n in names}


def read_listing(path):
    """{halfword offset: mnemonic or None for undefined}"""
    result = {}
    for line in open(path, errors="ignore"):
        m = re.match(r"\s*([0-9a-f]+):\s+([0-9a-f]{4})\s+(\S+)", line)
        if not m:
            continue
        mnemonic = m.group(3)
        undefined = mnemonic.startswith("<") or "undefined" in line.lower() or mnemonic in (".short", ".inst", ".word")
        result[int(m.group(1), 16) // 2] = None if undefined else mnemonic
    return result


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    names, decode = load_table()
    values = [v for v in range(65536) if (v >> 11) not in (0b11101, 0b11110, 0b11111)]
    for label, path in (("GNU objdump", sys.argv[1]), ("llvm-objdump", sys.argv[2])):
        listing = read_listing(path)
        mismatches = {}
        for index, insn in enumerate(values):
            name = decode(insn)
            mnemonic = listing.get(index)
            ok = (name == "UNDEFINED" and mnemonic is None) or \
                 (name != "UNDEFINED" and mnemonic is not None and bool(candidates(mnemonic) & accepted(name)))
            if not ok:
                mismatches.setdefault((name, mnemonic), []).append(insn)
        total = sum(len(v) for v in mismatches.values())
        print("%s: %d of %d halfwords agree, %d differ" % (label, len(values) - total, len(values), total))
        for (name, mnemonic), insns in sorted(mismatches.items(), key=lambda x: -len(x[1]))[:25]:
            print("  %-16s vs %-10s %5d  e.g. %04x" % (name, mnemonic, len(insns), insns[0]))


if __name__ == "__main__":
    main()

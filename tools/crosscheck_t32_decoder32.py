#!/usr/bin/env python3
"""Cross-check the generated 32-bit Thumb decoder against GNU objdump and
llvm-objdump on samples (docs/decoder.md).

32-bit instructions cannot all be checked, so samples are drawn with a fixed
seed: for every instruction of Arm's data, encodings with its fixed bits and
random field values; and random words over the whole 32-bit space. Each
sample is decoded by the generated decoder (test/decoder/decode_dump.c) and
by the two disassemblers.

A sample that does not agree is put in a class that explains it, using
Arm's data: what the A-profile tree decodes it to, and whether its should-be
bits hold. Samples left unexplained are listed.

Usage:
  python tools/crosscheck_t32_decoder32.py make <Instructions.json> <dir>
      writes <dir>/words.txt and <dir>/all32.bin
  (decode_dump < words.txt > ours.txt, and the two listings, see docs/decoder.md)
  python tools/crosscheck_t32_decoder32.py check <Instructions.json> <dir>
"""
import json
import os
import random
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from crosscheck_t32_decoder import ALIASES, candidates  # noqa: E402
from generate_t32_decoder import (Condition, decode_pattern, encoding_fields,  # noqa: E402
                                  encoding_matches, pattern_matches, strip)
from generate_t32_decoder32 import ARMV7M_AS, ARMV7M_FIXED, NOT_IN_ARMV7M_GROUPS, not_in_armv7m  # noqa: E402

SAMPLES_PER_INSTRUCTION = 16
RANDOM_WORDS = 20000

# 32-bit names whose mnemonics differ from the name (data name or its prefix).
ALIASES32 = dict(ALIASES)
ALIASES32.update({
    "ADD_i_T3": {"add", "cmn"}, "ADD_i_T4": {"add", "addw"},
    "SUB_i_T3": {"sub", "cmp"}, "SUB_i_T4": {"sub", "subw"},
    "ADD_r_T3": {"add", "cmn"}, "SUB_r_T2": {"sub", "cmp"},
    "AND_i_T1": {"and", "tst"}, "AND_r_T2": {"and", "tst"},
    "EOR_i_T1": {"eor", "teq"}, "EOR_r_T2": {"eor", "teq"},
    "ORR_i_T1": {"orr", "mov"}, "ORR_r_T2": {"orr", "mov", "lsl", "lsr", "asr", "ror", "rrx"},
    "ORN_i_T1": {"orn", "mvn"}, "ORN_r_T1": {"orn", "mvn"},
    "MOV_r_T3": {"mov", "lsl", "lsr", "asr", "ror", "rrx"},
    "MOV_rr_T2": {"lsl", "lsr", "asr", "ror"},
    "MOVT_T1": {"movt"}, "MOV_i_T3": {"movw", "mov"},
    "ADR_T2": {"sub", "subw", "adr"}, "ADR_T3": {"add", "addw", "adr"},
    "LDM_T2": {"ldm", "ldmia", "pop"}, "STMDB_T1": {"stmdb", "push"},
    "LDR_i_T4": {"ldr", "pop", "ldrt"}, "STR_i_T4": {"str", "push", "strt"},
    "B_T3": {"b"}, "B_T4": {"b"}, "BL_i_T1": {"bl"}, "BLX_i_T2": {"blx"},
    "TBB_T1": {"tbb", "tbh"},
    "MRS_T1_AS": {"mrs"}, "MSR_r_T1_AS": {"msr"},
    "VSTM": {"vstm", "vstmia"}, "VSTMDB": {"vstmdb", "vpush"},
    "VLDM": {"vldm", "vldmia", "vpop"}, "VLDMDB": {"vldmdb"},
    "ADD_SP_i_T4": {"add", "addw"}, "SUB_SP_i_T3": {"sub", "subw"},
    "MOV_r_T3_RRX": {"mov", "rrx"}, "MOVS_r_T3_RRX": {"movs", "rrxs"},
    "MOVS_rr_T2": {"lsls", "lsrs", "asrs", "rors"},
})

# Core register fields; PC or SP in one is UNPREDICTABLE in Armv7-M for almost
# every 32-bit instruction (the exceptions, like LDR to PC, agree anyway).
REGISTER_FIELDS = {"Rd", "Rt", "Rt2", "Rn", "Rm", "Ra", "Rs", "RdHi", "RdLo"}

COPROCESSOR = {"cdp", "ldc", "stc", "mcr", "mrc", "mcrr", "mrrc"}


def leaf_patterns(path):
    data = json.load(open(path))
    t32 = next(s for s in data["instructions"] if s["name"] == "T32")
    wide = next(c for c in t32["children"] if c["name"] == "w")
    leaves = []

    def walk(node):
        if node["_type"] == "Instruction.Instruction":
            bits = ["x"] * 32
            for value in node["encoding"]["values"]:
                start = value["range"]["start"]
                pattern = decode_pattern(value)
                for i, ch in enumerate(pattern):
                    bits[31 - (start + len(pattern) - 1 - i)] = ch
            leaves.append((node["name"], "".join(bits)))
            return
        for child in node.get("children", []):
            walk(child)

    walk(wide)
    return leaves


def make(path, out):
    rng = random.Random(20261103)
    words = []
    for _, pattern in leaf_patterns(path):
        for _ in range(SAMPLES_PER_INSTRUCTION):
            word = 0
            for ch in pattern:
                word = (word << 1) | (int(ch) if ch in "01" else rng.getrandbits(1))
            words.append(word)
    total = len(words) + RANDOM_WORDS
    while len(words) < total:
        word = rng.getrandbits(32)
        if (word >> 27) in (0b11101, 0b11110, 0b11111):
            words.append(word)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "words.txt"), "w") as f:
        f.writelines("%08x\n" % w for w in words)
    with open(os.path.join(out, "all32.bin"), "wb") as f:
        for w in words:
            f.write(struct.pack("<HH", w >> 16, w & 0xFFFF))
    print("%d samples" % len(words))


def read_listing(path):
    """{word offset: mnemonic or None}; a sample the listing does not start at
    its own address (the disassembler took it as two 16-bit ones) is None."""
    result = {}
    for line in open(path, errors="ignore"):
        m = re.match(r"\s*([0-9a-f]+):\s+((?:[0-9a-f]{4}\s?)+)\s+(\S+)", line)
        if not m or int(m.group(1), 16) % 4 != 0:
            continue
        length = len(m.group(2).split())
        mnemonic = m.group(3)
        undefined = length != 2 or mnemonic.startswith("<") or "undefined" in line.lower() \
            or mnemonic in (".short", ".inst", ".word", ".inst.w")
        result[int(m.group(1), 16) // 4] = None if undefined else mnemonic
    return result


def accepted(name):
    base = name.split("_")[0]
    names = ALIASES32.get(name) or ALIASES32.get(base) or {base.lower()}
    return set(names) | {n + "s" for n in names} | {n + suffix for n in names for suffix in (".w",)}


class Condition32(Condition):
    """Condition with the slices (field<3:2>, field<0>) 32-bit encodings use."""

    def eval(self, node, insn):
        if node["_type"] == "AST.BinaryOp" and node["left"]["_type"] == "AST.SquareOp":
            square = node["left"]
            arg = square["arguments"][0]
            start, _ = self.fields[square["var"]["value"]]
            high, low = (arg["left"]["value"], arg["right"]["value"]) if arg["_type"] == "AST.Slice" \
                else (arg["value"], arg["value"])
            width = high - low + 1
            value = (insn >> (start + low)) & ((1 << width) - 1)
            values = [node["right"]] if node["op"] in ("==", "!=") else node["right"]["values"]
            same = any(pattern_matches(strip(v["value"]), value, width) for v in values)
            return not same if node["op"] == "!=" else same
        if node["_type"] in ("AST.UnaryOp", "AST.BinaryOp") and node.get("op") in ("!", "&&", "||"):
            if node["op"] == "!":
                return not self.eval(node["expr"], insn)
            left = self.eval(node["left"], insn)
            return (left and self.eval(node["right"], insn)) if node["op"] == "&&" \
                else (left or self.eval(node["right"], insn))
        return super().eval(node, insn)


class ArmData:
    """The A-profile T32 tree evaluated on a word, without the Armv7-M rules."""

    def __init__(self, path):
        data = json.load(open(path))
        t32 = next(s for s in data["instructions"] if s["name"] == "T32")
        self.wide = next(c for c in t32["children"] if c["name"] == "w")

    def decode(self, word):
        """(instruction node, names of its groups) or (None, [])."""
        def walk(node, fields, groups):
            for child in node.get("children", []):
                child_fields = dict(fields)
                child_fields.update(encoding_fields(child["encoding"]))
                if not encoding_matches(child["encoding"], word):
                    continue
                cond = child.get("condition")
                if cond is not None and not Condition32(child_fields).eval(cond, word):
                    continue
                if child["_type"] == "Instruction.Instruction":
                    return child, groups
                found = walk(child, child_fields, groups + [child["name"]])
                if found[0] is not None:
                    return found
            return None, []
        return walk(self.wide, encoding_fields(self.wide["encoding"]), [])


def should_be_broken(node, word):
    """True when a should-be bit of the instruction does not hold
    (UNPREDICTABLE in Armv7-M)."""
    for value in node["encoding"]["values"]:
        start = value["range"]["start"]
        pattern = strip(value["value"]["value"])
        should_be = strip(value["should_be_mask"]["value"])
        width = len(pattern)
        for i, (ch, sb) in enumerate(zip(pattern, should_be)):
            if sb == "1" and ch in "01" and ((word >> (start + width - 1 - i)) & 1) != int(ch):
                return True
    return False


def field(node, word, name):
    for value in node["encoding"]["values"]:
        if value.get("name") == name:
            return (word >> value["range"]["start"]) & ((1 << value["range"]["width"]) - 1)
    return None


def pc_or_sp_operand(node, word):
    if any(field(node, word, f) in (13, 15) for f in REGISTER_FIELDS):
        return True
    name = node["name"]
    if name.startswith(("STM", "LDM")):
        # Register list (with P and M): SP never; PC in a store; PC and LR both in a load.
        regs = word & 0xFFFF
        return bool(regs & 0x2000) or (name.startswith("STM") and bool(regs & 0x8000)) or \
            (regs & 0xC000) == 0xC000 or bin(regs).count("1") < 2
    return False


def beyond_d15(node, word):
    """A double-precision register number of 16 or more: Cortex-M7 (FPv5-D16)
    has D0-D15. For VLDM/VSTM, a list that is empty or runs past D15."""
    name = node["name"]
    if re.match(r"V(LD|ST)M(DB)?_T1$", name):
        first = (field(node, word, "D") << 4) | field(node, word, "Vd")
        count = field(node, word, "imm8") // 2
        return count == 0 or first + count > 16
    # The fields holding bit 4 of a double-precision register number.
    double_fields = {"VCVT_ds_T1": ("D",), "VCVT_sd_T1": ("M",), "VMOV_tod_T1": ("M",),
                     "VMOV_d_T1": ("M",), "VMOV_rs_T1": ("D",), "VMOV_sr_T1": ("N",)}
    if name in double_fields:
        fields = double_fields[name]
    elif name.endswith("_DH"):
        fields = ("D",)
    elif name.endswith("_HD"):
        fields = ("M",)
    elif name.endswith("_D"):
        fields = ("D", "N", "M")
    else:
        return False
    return any(field(node, word, f) == 1 for f in fields)


def classify(arm, word, name, mnemonic):
    """Why a sample does not agree, or None when it is not explained."""
    node, groups = arm.decode(word)
    if name == "UNDEFINED" and mnemonic is not None:
        if candidates(mnemonic) & {c + s for c in COPROCESSOR for s in ("", "2", "l", "2l")}:
            return "coprocessor instruction (Cortex-M7: NOCP UsageFault)"
        if node is None:
            return "not decoded by Arm's A-profile data (other architectures, UNPREDICTABLE)"
        if set(groups) & NOT_IN_ARMV7M_GROUPS or not_in_armv7m(node["name"]):
            return "not in Armv7-M (A-profile, Advanced SIMD, Armv8)"
        if node["name"] in ARMV7M_FIXED:
            return "not in Armv7-M (A-profile, Advanced SIMD, Armv8)"
        if should_be_broken(node, word):
            return "UNPREDICTABLE: should-be bits"
        return None
    if node is not None and node["name"] in ARMV7M_AS:
        return "executed as %s in Armv7-M (unallocated hint, DSB option, reserved SYSm)" \
            % ARMV7M_AS[node["name"]]
    if node is not None and re.search(r"_l_", node["name"]) and mnemonic is not None and \
            candidates(mnemonic) & {m + "t" for m in accepted(node["name"])}:
        return "unprivileged load with Rn = PC: the literal load in Armv7-M"
    if node is not None:
        if should_be_broken(node, word):
            return "UNPREDICTABLE: should-be bits"
        if beyond_d15(node, word):
            return "D16-D31 (Cortex-M7 has D0-D15)"
        if pc_or_sp_operand(node, word):
            return "UNPREDICTABLE: PC or SP as an operand"
    return None


def check(path, out):
    arm = ArmData(path)
    ours = [line.split()[1] for line in open(os.path.join(out, "ours.txt"))]
    words = [int(line, 16) for line in open(os.path.join(out, "words.txt"))]
    for label, listing in (("GNU objdump", "gnu32.txt"), ("llvm-objdump", "llvm32.txt")):
        theirs = read_listing(os.path.join(out, listing))
        classes = {}
        unexplained = {}
        for index, name in enumerate(ours):
            mnemonic = theirs.get(index)
            ok = (name == "UNDEFINED" and mnemonic is None) or \
                 (name != "UNDEFINED" and mnemonic is not None and bool(candidates(mnemonic) & accepted(name)))
            if ok:
                continue
            why = classify(arm, words[index], name, mnemonic)
            if why:
                classes[why] = classes.get(why, 0) + 1
            else:
                unexplained.setdefault((name, mnemonic.lower() if mnemonic else None), []).append(index)
        differ = sum(classes.values()) + sum(len(v) for v in unexplained.values())
        print("%s: %d of %d samples agree, %d differ" % (label, len(ours) - differ, len(ours), differ))
        for why, count in sorted(classes.items(), key=lambda x: -x[1]):
            print("  %6d  %s" % (count, why))
        print("  %6d  unexplained" % sum(len(v) for v in unexplained.values()))
        for (name, mnemonic), idx in sorted(unexplained.items(), key=lambda x: -len(x[1]))[:40]:
            print("      %-18s vs %-14s %5d  e.g. %08x" % (name, mnemonic, len(idx), words[idx[0]]))


def main():
    if len(sys.argv) == 4 and sys.argv[1] == "make":
        make(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 4 and sys.argv[1] == "check":
        check(sys.argv[2], sys.argv[3])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()

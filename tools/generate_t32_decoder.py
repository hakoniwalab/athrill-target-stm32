#!/usr/bin/env python3
"""Generate the 16-bit Thumb decoder of Athrill's STM32 target from Arm's
machine-readable architecture data (AARCHMRS, Instructions.json).

The T32 instruction set in the data is a tree of instruction groups. Each node
has an encoding (fixed bits and named fields over a 16- or 32-bit word) and a
decode condition over its fields. For every 16-bit value this script evaluates
the whole tree and records which instruction it decodes to, so the generated
decoder is one table lookup with the exact meaning of the data (fixed bits and
all conditions). Values whose bits 15:11 are 0b11101, 0b11110 or 0b11111 are
the first halfword of a 32-bit instruction.

Armv7-M implements none of the A-profile features the data asks about
(IsFeatureImplemented() is false), and the A-profile-only instructions in
NOT_IN_ARMV7M decode as undefined; hints that Armv7-M does not allocate
(0xBFx0) decode as NOP, as Armv7-M executes them. Which instructions are executed is decided
by the executor (src/cpu/thumb_exec16.c), not here.

Usage:
  python tools/generate_t32_decoder.py <path to Instructions.json> <package name>
writes src/cpu/decoder/t32_decode16.h and t32_decode16.c.
"""
import hashlib
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "src", "cpu", "decoder")

NOTICE = """\
 * Derived from Arm's Architecture Machine Readable Specification (AARCHMRS),
 * package {package} (Instructions.json, SHA-256 {sha256}).
 *
 * Copyright 2010-2026 Arm Limited or its affiliates.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the copyright holder nor the names of its contributors
 *   may be used to endorse or promote products derived from this software
 *   without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE."""


def pattern_matches(pattern, value, width):
    """'1x0' style pattern against the width-bit value (MSB first)."""
    for i, ch in enumerate(pattern):
        bit = (value >> (width - 1 - i)) & 1
        if ch in "01" and int(ch) != bit:
            return False
    return True


def field_value(insn, field):
    start, width = field
    return (insn >> start) & ((1 << width) - 1)


def strip(value):
    return value.strip("'")


class Condition:
    """Evaluates a decode condition (AST) over the fields of a node."""

    def __init__(self, fields):
        self.fields = fields

    def eval(self, node, insn):
        kind = node["_type"]
        if kind == "AST.Bool":
            return node["value"]
        if kind == "AST.UnaryOp" and node["op"] == "!":
            return not self.eval(node["expr"], insn)
        if kind == "AST.Function" and node["name"] == "IsFeatureImplemented":
            return False
        if kind == "AST.BinaryOp":
            op = node["op"]
            if op == "&&":
                return self.eval(node["left"], insn) and self.eval(node["right"], insn)
            if op == "||":
                return self.eval(node["left"], insn) or self.eval(node["right"], insn)
            name = node["left"]["value"]
            field = self.fields[name]
            value = field_value(insn, field)
            if op in ("==", "!="):
                same = pattern_matches(strip(node["right"]["value"]), value, field[1])
                return same if op == "==" else not same
            if op == "IN":
                return any(pattern_matches(strip(v["value"]), value, field[1])
                           for v in node["right"]["values"])
        raise ValueError("unsupported condition: " + json.dumps(node)[:200])


def decode_pattern(value):
    """The value's pattern with its should-be bits (should_be_mask '1') as 'x':
    those bits should have the given value, but the instruction decodes as
    this one whatever they are (otherwise UNPREDICTABLE), as the hardware and
    the disassemblers do."""
    pattern = strip(value["value"]["value"])
    should_be = strip(value["should_be_mask"]["value"])
    return "".join("x" if sb == "1" else ch for ch, sb in zip(pattern, should_be))


def encoding_matches(encoding, insn):
    for value in encoding["values"]:
        start = value["range"]["start"]
        width = value["range"]["width"]
        if not pattern_matches(decode_pattern(value), field_value(insn, (start, width)), width):
            return False
    return True


def encoding_fields(encoding):
    return {v["name"]: (v["range"]["start"], v["range"]["width"])
            for v in encoding["values"] if v["_type"] == "Instruction.Encodeset.Field"}


def collect_16bit(t32):
    """The 16-bit instructions in tree order: (name, [(encoding, condition, fields)...], leaf fields)."""
    leaves = []

    def walk(node, path):
        encoding = node.get("encoding")
        here = path
        if encoding is not None and encoding["width"] == 16:
            here = path + [(encoding, node.get("condition"), encoding_fields(encoding))]
        elif encoding is not None and node is not t32:
            return  # a 32-bit subtree
        if node["_type"] == "Instruction.Instruction":
            fields = [(v["name"], v["range"]["start"], v["range"]["width"])
                      for v in node["encoding"]["values"]
                      if v["_type"] == "Instruction.Encodeset.Field" and "x" in strip(v["value"]["value"])]
            leaves.append((node["name"], here, fields))
            return
        for child in node.get("children", []):
            walk(child, here)

    walk(t32, [])
    return leaves


# A-profile instructions that Armv7-M does not have: decoded as undefined.
NOT_IN_ARMV7M = {"HLT_T1", "SETEND_T1", "SETPAN_T1"}


def is_32bit_prefix(insn):
    return (insn >> 11) in (0b11101, 0b11110, 0b11111)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    path, package = sys.argv[1], sys.argv[2]
    with open(path, "rb") as f:
        raw = f.read()
    sha256 = hashlib.sha256(raw).hexdigest()
    data = json.loads(raw)
    t32 = next(s for s in data["instructions"] if s["name"] == "T32")
    leaves = collect_16bit(t32)

    # Instruction ids: 0 = undefined, 1 = first halfword of a 32-bit instruction.
    table = [0] * 65536
    conflicts = []
    for insn in range(65536):
        if is_32bit_prefix(insn):
            table[insn] = 1
            continue
        found = []
        for index, (name, path_nodes, _) in enumerate(leaves):
            if name in NOT_IN_ARMV7M:
                continue
            if all(encoding_matches(enc, insn) and
                   (cond is None or Condition(fields).eval(cond, insn))
                   for enc, cond, fields in path_nodes):
                found.append(index)
        if len(found) > 1:
            conflicts.append((insn, [leaves[i][0] for i in found]))
        if found:
            table[insn] = found[0] + 2
    # Armv7-M: the hint space (0xBFx0) not allocated to a v7-M hint is
    # "NOP-compatible" and executes as NOP (SEVL is an Armv8 hint).
    nop_id = 2 + [name for name, _, _ in leaves].index("NOP_T1")
    sevl_id = 2 + [name for name, _, _ in leaves].index("SEVL_T1")
    for insn in range(0xBF00, 0xC000, 0x10):
        if table[insn] in (0, sevl_id):
            table[insn] = nop_id
    if conflicts:
        for insn, names in conflicts[:20]:
            print("conflict %04x: %s" % (insn, ", ".join(names)), file=sys.stderr)
        sys.exit("%d encodings decode to more than one instruction" % len(conflicts))

    # Two-level table: bits 15:8 select a 256-entry block; identical blocks are shared.
    blocks = []
    level1 = []
    for high in range(256):
        block = tuple(table[high * 256:(high + 1) * 256])
        if block not in blocks:
            blocks.append(block)
        level1.append(blocks.index(block))

    names = ["UNDEFINED", "WIDE"] + [name for name, _, _ in leaves]
    header = ["/*",
              " * Generated by tools/generate_t32_decoder.py. Do not edit.",
              " *",
              " * 16-bit Thumb (T32) decoder: t32_decode16() gives the instruction id of",
              " * a halfword; T32_16_WIDE marks the first halfword of a 32-bit instruction.",
              " *",
              NOTICE.format(package=package, sha256=sha256),
              " */",
              "#ifndef T32_DECODE16_H",
              "#define T32_DECODE16_H",
              "",
              "#include <stdint.h>",
              "",
              "typedef enum {"]
    for i, name in enumerate(names):
        header.append("\tT32_16_%s = %d," % (name, i))
    header += ["\tT32_16_ID_NUM = %d" % len(names), "} T32Decode16Id;", "",
               "extern const char *const t32_decode16_names[T32_16_ID_NUM];",
               "extern const uint8_t t32_decode16_level1[256];",
               "extern const uint8_t t32_decode16_blocks[%d][256];" % len(blocks), "",
               "static inline T32Decode16Id t32_decode16(uint16_t insn)",
               "{",
               "\treturn (T32Decode16Id)t32_decode16_blocks[t32_decode16_level1[insn >> 8]][insn & 0xFFU];",
               "}", "",
               "/* Fields of each instruction: t32_<instruction>_<field>(insn). */"]
    for name, _, fields in leaves:
        for field, start, width in fields:
            header.append("static inline uint32_t t32_%s_%s(uint16_t insn) { return ((uint32_t)insn >> %d) & 0x%XU; }"
                          % (name, field, start, (1 << width) - 1))
    header += ["", "#endif", ""]

    source = ["/*", " * Generated by tools/generate_t32_decoder.py. Do not edit.", " *",
              NOTICE.format(package=package, sha256=sha256), " */",
              '#include "t32_decode16.h"', "",
              "const char *const t32_decode16_names[T32_16_ID_NUM] = {"]
    source += ['\t"%s",' % name for name in names]
    source += ["};", "", "const uint8_t t32_decode16_level1[256] = {"]
    for i in range(0, 256, 16):
        source.append("\t" + " ".join("%d," % v for v in level1[i:i + 16]))
    source += ["};", "", "const uint8_t t32_decode16_blocks[%d][256] = {" % len(blocks)]
    for block in blocks:
        source.append("\t{")
        for i in range(0, 256, 16):
            source.append("\t\t" + " ".join("%d," % v for v in block[i:i + 16]))
        source.append("\t},")
    source += ["};", ""]

    os.makedirs(OUT_DIR, exist_ok=True)
    with open(os.path.join(OUT_DIR, "t32_decode16.h"), "w") as f:
        f.write("\n".join(header))
    with open(os.path.join(OUT_DIR, "t32_decode16.c"), "w") as f:
        f.write("\n".join(source))
    decoded = sum(1 for v in table if v >= 2)
    print("%d instructions, %d of 65536 values decode to one, %d are 32-bit prefixes, %d undefined, %d blocks"
          % (len(leaves), decoded, table.count(1), table.count(0), len(blocks)))


if __name__ == "__main__":
    main()

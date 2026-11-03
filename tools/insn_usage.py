#!/usr/bin/env python3
"""Which Thumb instructions a firmware uses, by the generated decoders' names,
and which of them this target executes.

The firmware's disassembly (arm-none-eabi-objdump -d) gives each function's
instructions; their encodings are named by the generated decoders (through
decode_dump, built by CMake). Two sets are counted:

- startup: the functions reachable from the entry symbol through direct
  branches and calls (B, BL, CBZ/CBNZ, B<c> to another function); calls
  through pointers (tasks, callbacks) are not followed;
- all: every function of the firmware.

Usage:
  python tools/insn_usage.py <objdump listing> <decode_dump> [entry symbol]
                             [--stop=<symbol>,...] [--functions]
--stop leaves the given functions (and what only they reach) out of the
startup set, for example --stop=nx_start for the code before the OS starts.
prints, per instruction, its count in the startup set and in the whole
firmware, and whether src/cpu/thumb_exec16.c / thumb_exec32.c execute it.
"""
import os
import re
import subprocess
import sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FUNC_RE = re.compile(r"^([0-9a-f]+) <([^>]+)>:$")
INSN_RE = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f]{4})(?: ([0-9a-f]{4}))?\s+(\S+)\s*(.*)$")
TARGET_RE = re.compile(r"<([^>+]+)(?:\+0x[0-9a-f]+)?>")
DIRECT_BRANCHES = ("b", "bl", "cbz", "cbnz")
CONDITIONS = ("eq", "ne", "cs", "hs", "cc", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "al")


def is_direct_branch(mnemonic):
    m = mnemonic.split(".")[0]
    if m in DIRECT_BRANCHES:
        return True
    return m.startswith("b") and m[1:] in CONDITIONS


def read_listing(path):
    """{function: [(address, encoding hex, mnemonic, operands)]}"""
    functions = defaultdict(list)
    current = None
    for line in open(path, errors="ignore"):
        line = line.rstrip("\n")
        m = FUNC_RE.match(line)
        if m:
            current = m.group(2)
            continue
        m = INSN_RE.match(line)
        if not m or current is None:
            continue
        mnemonic = m.group(4)
        if mnemonic.startswith("."):
            continue	# data (.word, .short) in the text
        encoding = m.group(2) + (m.group(3) or "")
        functions[current].append((int(m.group(1), 16), encoding, mnemonic, m.group(5)))
    return functions


def reachable(functions, entry, stop=()):
    seen = set(stop)
    todo = [entry]
    while todo:
        name = todo.pop()
        if name in seen or name not in functions:
            continue
        seen.add(name)
        for _, _, mnemonic, operands in functions[name]:
            if not is_direct_branch(mnemonic):
                continue
            m = TARGET_RE.search(operands)
            if m and m.group(1) != name:
                todo.append(m.group(1))
    return seen - set(stop)


def decode_names(decode_dump, encodings):
    out = subprocess.run([decode_dump], input="\n".join(encodings) + "\n", capture_output=True, text=True, check=True)
    return dict(line.split() for line in out.stdout.splitlines())


def executed_ids():
    ids = set()
    for name, width in (("thumb_exec16.c", "16"), ("thumb_exec32.c", "32")):
        text = open(os.path.join(ROOT, "src", "cpu", name)).read()
        ids |= set(re.findall(r"\[T32_%s_([A-Za-z0-9_]+)\]" % width, text))
    return ids


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) not in (2, 3):
        sys.exit(__doc__)
    listing, decode_dump = args[0], args[1]
    entry = args[2] if len(args) == 3 else "__start"
    functions = read_listing(listing)
    stop = []
    for a in sys.argv[1:]:
        if a.startswith("--stop="):
            stop = a[len("--stop="):].split(",")
    startup = reachable(functions, entry, stop)
    encodings = sorted({e for insns in functions.values() for _, e, _, _ in insns})
    names = decode_names(decode_dump, encodings)
    executed = executed_ids()

    count_all = Counter()
    count_startup = Counter()
    example = {}
    for func, insns in functions.items():
        for _, encoding, mnemonic, operands in insns:
            name = names[encoding]
            count_all[name] += 1
            if func in startup:
                count_startup[name] += 1
                example.setdefault(name, "%s %s" % (mnemonic, operands))

    startup_insns = sum(len(functions[f]) for f in startup)
    print("entry %s: %d functions reachable, %d instructions; firmware: %d functions, %d instructions"
          % (entry, len(startup), startup_insns, len(functions), sum(count_all.values())))
    used = sorted(count_startup, key=lambda n: -count_startup[n])
    missing = [n for n in used if n not in executed]
    print("startup uses %d instructions (encodings); %d executed, %d not yet" % (len(used), len(used) - len(missing), len(missing)))
    print("%-22s %8s %8s  %-4s %s" % ("instruction", "startup", "all", "exec", "example"))
    for name in used:
        print("%-22s %8d %8d  %-4s %s" % (name, count_startup[name], count_all[name],
                                         "yes" if name in executed else "-", example[name][:50]))
    if "--functions" in sys.argv:
        print("\nstartup functions:")
        for f in sorted(startup):
            print("  " + f)


if __name__ == "__main__":
    main()

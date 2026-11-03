#!/usr/bin/env python3
"""Which instruction encodings the test programs of test/insn use, against
the encodings that have an executor (hand-written or generated).

Disassembles each test ELF with the toolchain image's objdump (Docker),
names every instruction with the generated decoders (decode_dump, built by
CMake with BUILD_TESTING) and lists the executor encodings no test program
contains.

Usage:
  python tools/check_insn_tests.py <decode_dump>
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INSN = os.path.join(ROOT, "test", "insn")


def executor_ids():
    ids = set()
    for name, width in (("thumb_exec16.c", 16), ("thumb_exec32.c", 32), ("thumb_gen16.c", 16), ("thumb_gen32.c", 32)):
        text = open(os.path.join(ROOT, "src", "cpu", name)).read()
        ids |= set(re.findall(r"\[T32_%d_(\w+)\] = exec_" % width, text))
    return ids


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    elfs = sorted(f for f in os.listdir(INSN) if f.endswith(".elf"))
    listing = subprocess.run(["docker", "run", "--rm", "-v", INSN + ":/w", "-w", "/w", "athrill-stm32-toolchain",
                              "sh", "-c", " ".join("arm-none-eabi-objdump -d %s;" % e for e in elfs)],
                             capture_output=True, text=True, check=True).stdout
    encodings = []
    for line in listing.splitlines():
        m = re.match(r"^\s*[0-9a-f]+:\s+([0-9a-f]{4})(?: ([0-9a-f]{4}))?\s+(\S+)", line)
        if m and not m.group(3).startswith("."):
            encodings.append(m.group(1) + (m.group(2) or ""))
    out = subprocess.run([sys.argv[1]], input="\n".join(encodings) + "\n", capture_output=True, text=True, check=True).stdout
    used = {line.split()[1] for line in out.splitlines()}
    ids = executor_ids()
    missing = sorted(ids - used)
    print("%d encodings with an executor; in the test programs: %d; not in any: %s"
          % (len(ids), len(ids) - len(missing), " ".join(missing) if missing else "none"))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())

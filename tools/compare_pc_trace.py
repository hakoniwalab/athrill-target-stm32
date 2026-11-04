#!/usr/bin/env python3
"""Compare two Renode-format PC traces without loading them into memory."""

import argparse
import bisect
import re
import shutil
import subprocess
import sys
from collections import deque


def read_pc(stream, path, line_number):
    line = stream.readline()
    if line == "":
        return None
    try:
        return int(line.strip(), 16)
    except ValueError as error:
        raise ValueError(f"{path}:{line_number}: invalid PC: {line.rstrip()!r}") from error


class Symbols:
    def __init__(self, entries):
        entries.sort()
        self.addresses = [entry[0] for entry in entries]
        self.names = [entry[1] for entry in entries]

    def at(self, pc):
        if pc is None or not self.addresses:
            return ""
        index = bisect.bisect_right(self.addresses, pc) - 1
        if index < 0:
            return ""
        offset = pc - self.addresses[index]
        suffix = "" if offset == 0 else f"+0x{offset:x}"
        return f" {self.names[index]}{suffix}"


def nm_symbols(elf):
    for program in ("arm-none-eabi-nm", "llvm-nm"):
        executable = shutil.which(program)
        if executable is None:
            continue
        result = subprocess.run(
            [executable, "-n", "--defined-only", elf],
            check=False, capture_output=True, text=True)
        if result.returncode != 0:
            continue
        entries = []
        for line in result.stdout.splitlines():
            match = re.match(r"^([0-9A-Fa-f]+)\s+\S\s+(.+)$", line)
            if match:
                entries.append((int(match.group(1), 16), match.group(2)))
        return Symbols(entries), program
    return None, None


def elf_symbols(elf):
    symbols, source = nm_symbols(elf)
    if symbols is not None:
        return symbols, source
    try:
        from elftools.elf.elffile import ELFFile
    except ImportError:
        return Symbols([]), None
    entries = []
    with open(elf, "rb") as stream:
        elf_file = ELFFile(stream)
        for section in elf_file.iter_sections():
            if section.header.sh_type != "SHT_SYMTAB":
                continue
            for symbol in section.iter_symbols():
                if symbol.name and symbol.entry.st_shndx != "SHN_UNDEF":
                    entries.append((symbol.entry.st_value, symbol.name))
    return Symbols(entries), "pyelftools"


def format_pc(pc, symbols):
    if pc is None:
        return "<EOF>"
    return f"0x{pc:08X}{symbols.at(pc)}"


def print_context(rows, symbols, label_a, label_b):
    print(f"\n{label_a}:")
    for index, pc_a, _pc_b in rows:
        marker = ">" if pc_a != _pc_b else " "
        print(f"{marker} {index:>12}: {format_pc(pc_a, symbols)}")
    print(f"\n{label_b}:")
    for index, _pc_a, pc_b in rows:
        marker = ">" if _pc_a != pc_b else " "
        print(f"{marker} {index:>12}: {format_pc(pc_b, symbols)}")


def compare(path_a, path_b, symbols, context):
    before = deque(maxlen=max(0, context // 2 - 1))
    with open(path_a, encoding="ascii") as trace_a, open(path_b, encoding="ascii") as trace_b:
        index = 0
        while True:
            pc_a = read_pc(trace_a, path_a, index + 1)
            pc_b = read_pc(trace_b, path_b, index + 1)
            if pc_a == pc_b:
                if pc_a is None:
                    print(f"identical: {index} instruction PCs")
                    return 0
                before.append((index, pc_a, pc_b))
                index += 1
                continue

            rows = list(before)
            rows.append((index, pc_a, pc_b))
            after = context - len(rows)
            for offset in range(1, after + 1):
                next_a = read_pc(trace_a, path_a, index + offset + 1) if pc_a is not None else None
                next_b = read_pc(trace_b, path_b, index + offset + 1) if pc_b is not None else None
                rows.append((index + offset, next_a, next_b))
                if next_a is None and next_b is None:
                    break
            print(f"first difference at zero-based instruction index {index}")
            print_context(rows, symbols, path_a, path_b)
            return 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace_a")
    parser.add_argument("trace_b")
    parser.add_argument("--elf", help="ELF used to resolve trace PCs")
    parser.add_argument("--context", type=int, default=20,
                        help="total context lines per trace (default: 20)")
    args = parser.parse_args()
    if args.context < 1:
        parser.error("--context must be positive")
    symbols = Symbols([])
    if args.elf:
        symbols, source = elf_symbols(args.elf)
        if source:
            print(f"symbols: {source} ({len(symbols.addresses)} entries)")
        else:
            print("symbols: unavailable", file=sys.stderr)
    try:
        return compare(args.trace_a, args.trace_b, symbols, args.context)
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    sys.exit(main())

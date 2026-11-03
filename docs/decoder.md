# Thumb decoder

The 16-bit Thumb decoder is generated from Arm's Architecture Machine Readable
Specification (AARCHMRS), as decided in [design.md](design.md) (decision 10).
The 32-bit instructions follow the same way.

## Source

Arm's open-source A-profile package
`AARCHMRS_OPENSOURCE_A_profile_FAT-2026-09` (`Instructions.json`, which
contains the A64, A32 and T32 instruction sets), from
<https://developer.arm.com/-/cdn-downloads/permalink/Exploration-Tools-OS-Machine-Readable-Data/AARCHMRS_BSD/AARCHMRS_OPENSOURCE_A_profile_FAT-2026-09.tar.gz>.
It is provided under a BSD 3-clause licence (`docs/notice.html` in the
package; Copyright 2010-2026 Arm Limited or its affiliates). The generated
files carry that notice and the SHA-256 of the `Instructions.json` they were
made from. The package itself is not kept in this repository.

## How it is generated

`tools/generate_t32_decoder.py` reads the T32 instruction set, a tree of
instruction groups whose nodes carry an encoding (fixed bits and named fields)
and a decode condition. For each of the 65,536 halfwords it evaluates the
whole tree, so the table means exactly what the data says, conditions
included. The result, `src/cpu/decoder/t32_decode16.{h,c}`, is a two-level
table (bits 15:8, then 7:0; identical blocks shared) giving an instruction id,
plus one accessor per instruction field (`t32_<instruction>_<field>()`).

Armv7-M is not the A-profile, so the generator applies these rules:

- Bits the data marks as "should be" a value (for example BX's bits 2:0) do
  not take part in decoding: the instruction decodes whatever they are
  (otherwise UNPREDICTABLE), as the hardware and the disassemblers do.
- `IsFeatureImplemented()` is false: Armv7-M has none of the A-profile
  features the data asks about.
- HLT, SETEND and SETPAN, which Armv7-M does not have, decode as undefined.
- Hints Armv7-M does not allocate (0xBFx0, including SEVL) decode as NOP:
  Armv7-M executes them as NOP.

Which instructions run is decided by `src/cpu/thumb_exec16.c`, which has an
executor per instruction id; an instruction without one stops the run with
its name.

```bash
mkdir -p aarchmrs
curl -L --fail -o aarchmrs/AARCHMRS.tar.gz "https://developer.arm.com/-/cdn-downloads/permalink/Exploration-Tools-OS-Machine-Readable-Data/AARCHMRS_BSD/AARCHMRS_OPENSOURCE_A_profile_FAT-2026-09.tar.gz"
tar -xzf aarchmrs/AARCHMRS.tar.gz -C aarchmrs
python tools/generate_t32_decoder.py aarchmrs/Instructions.json AARCHMRS_OPENSOURCE_A_profile_FAT-2026-09
```

## Cross-check

`tools/crosscheck_t32_decoder.py` compares the table with GNU objdump and
llvm-objdump (Cortex-M7) for every halfword that is not the first half of a
32-bit instruction (59,392 values), mapping the data's instruction names to
the disassemblers' mnemonics. The listings are made in the toolchain image:

```bash
python -c "import struct; open('all16.bin','wb').write(b''.join(struct.pack('<H', v) for v in range(65536) if (v >> 11) not in (29, 30, 31)))"
docker run --rm -v "$PWD:/w" -w /w athrill-stm32-toolchain sh -c 'arm-none-eabi-objdump -D -b binary -m arm -M force-thumb all16.bin > gnu.txt && arm-none-eabi-objcopy -I binary -O elf32-littlearm -B arm --rename-section .data=.text,contents,alloc,load,readonly,code all16.bin all16.o && llvm-objdump -d -z --triple=thumbv7em-none-eabi --mcpu=cortex-m7 all16.o > llvm.txt'
python tools/crosscheck_t32_decoder.py gnu.txt llvm.txt
```

Result with the package above:

| Disassembler | Agree | Differ |
|---|---|---|
| GNU objdump (arm-none-eabi 13.2) | 59,179 | 213 |
| llvm-objdump (Cortex-M7) | 59,246 | 146 |

Every remaining difference is one of these:

| Halfwords | Decoder | Disassemblers | Why |
|---|---|---|---|
| HLT, SETEND, SETPAN (68) | undefined | GNU: the instruction; LLVM: undefined | Not in Armv7-M; GNU disassembles for a later architecture |
| BX/BLX with bits 2:0 set (32 shown as `bxns`/`blxns` by GNU) | BX, BLX | GNU: Armv8-M security instructions | Not in Armv7-M; there they are BX/BLX with should-be-zero bits set |
| BLX with should-be-zero bits set (96 GNU, 112 LLVM) | BLX | undefined | UNPREDICTABLE encodings; the data decodes them |
| LDM, STM, PUSH, POP with an empty register list (18, LLVM) | the instruction | undefined | UNPREDICTABLE encodings |
| CPS with the A flag (16) | CPSIE/CPSID | undefined | A-profile form; UNPREDICTABLE in Armv7-M |
| 0xBF50 (1, GNU) | NOP | `sevl` | SEVL is an Armv8 hint; Armv7-M executes it as NOP |

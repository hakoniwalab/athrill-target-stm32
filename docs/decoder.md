# Thumb decoder

The Thumb decoders, 16-bit and 32-bit, are generated from Arm's Architecture
Machine Readable Specification (AARCHMRS), as decided in
[design.md](design.md) (decision 10). The generators, the cross-check tools
and the Armv7-M rules (`rules_armv7m.py`) are in
[athrill-arm-common](https://github.com/hakoniwalab/athrill-arm-common)
(`tools/`), shared with Athrill's other Arm targets; this target keeps the
generated files and the results below. The commands are run from this
checkout, with athrill-arm-common next to it.

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

`tools/generate_t32_decoder16.py` reads the T32 instruction set, a tree of
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

Which instructions run is decided by `src/cpu/thumb_exec16.c` and `thumb_exec32.c`, which have an
executor per instruction id; an instruction without one stops the run with
its name.

```bash
mkdir -p aarchmrs
curl -L --fail -o aarchmrs/AARCHMRS.tar.gz "https://developer.arm.com/-/cdn-downloads/permalink/Exploration-Tools-OS-Machine-Readable-Data/AARCHMRS_BSD/AARCHMRS_OPENSOURCE_A_profile_FAT-2026-09.tar.gz"
tar -xzf aarchmrs/AARCHMRS.tar.gz -C aarchmrs
python ../athrill-arm-common/tools/generate_t32_decoder16.py armv7m aarchmrs/Instructions.json AARCHMRS_OPENSOURCE_A_profile_FAT-2026-09 src/cpu/decoder
```

### 32-bit instructions

32-bit instructions are too many to enumerate, so
`tools/generate_t32_decoder32.py` turns the 32-bit part of the T32 tree into C
code: one function per instruction group, which tests each child's fixed bits
and decode condition in the data's order and returns the first instruction
that matches. The result, `src/cpu/decoder/t32_decode32.{h,c}`, gives
`t32_decode32()` for a word with the first halfword in bits 31:16, plus one
accessor per instruction field (`t32w_<instruction>_<field>()`).

The target is Cortex-M7 (Armv7E-M with the FPv5 floating-point unit, 16
double-precision registers). Besides the should-be and
`IsFeatureImplemented()` rules above, the generator applies:

- Groups Armv7-M does not have decode as undefined: Advanced SIMD (NEON) and
  its extensions, Armv8 load-acquire/store-release, 32-bit CPS, BXJ, ERET,
  DCPS and BLX with an immediate.
- Instructions Armv7-M does not have decode as undefined: SRS, RFE, HVC, SMC,
  SB, CRC32, PLDW, LDREXD, STREXD, FLDMX/FSTMX, VDUP, VJCVT, the BFloat16
  conversions, and half-precision arithmetic and moves (Cortex-M7 has only the
  half-precision conversions VCVTB/VCVTT).
- The FPv5 instructions from Armv8 stay: VSEL, VMAXNM, VMINNM, VRINTA/N/P/M
  and VCVTA/N/P/M.
- Some instructions decode as the one Armv7-M executes: hints Armv7-M does not
  allocate (SEVL, ESB, TSB, CSDB, CLRBHB) as NOP; SSBB and PSSBB as DSB (DSB
  with any option is DSB); the banked-register MRS/MSR forms as MRS/MSR with a
  reserved SYSm value.
- VMOV between a core register and a scalar moves only a 32-bit word; its 8-
  and 16-bit forms are Advanced SIMD and decode as undefined.

```bash
python ../athrill-arm-common/tools/generate_t32_decoder32.py armv7m aarchmrs/Instructions.json AARCHMRS_OPENSOURCE_A_profile_FAT-2026-09 src/cpu/decoder
```

## Cross-check

### 16-bit instructions

`tools/crosscheck_t32_decoder16.py` compares the table with GNU objdump and
llvm-objdump (Cortex-M7) for every halfword that is not the first half of a
32-bit instruction (59,392 values), mapping the data's instruction names to
the disassemblers' mnemonics. The listings are made in the toolchain image:

```bash
python -c "import struct; open('all16.bin','wb').write(b''.join(struct.pack('<H', v) for v in range(65536) if (v >> 11) not in (29, 30, 31)))"
docker run --rm -v "$PWD:/w" -w /w athrill-stm32-toolchain sh -c 'arm-none-eabi-objdump -D -b binary -m arm -M force-thumb all16.bin > gnu.txt && arm-none-eabi-objcopy -I binary -O elf32-littlearm -B arm --rename-section .data=.text,contents,alloc,load,readonly,code all16.bin all16.o && llvm-objdump -d -z --triple=thumbv7em-none-eabi --mcpu=cortex-m7 all16.o > llvm.txt'
python ../athrill-arm-common/tools/crosscheck_t32_decoder16.py src/cpu/decoder gnu.txt llvm.txt
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

### 32-bit instructions

`tools/crosscheck_t32_decoder32.py` checks samples drawn with a fixed seed:
16 for each instruction of the data (its fixed bits, random fields) and 20,000
random 32-bit words, 36,608 in all. `test/decoder/decode_dump.c` prints the
generated decoder's names; built by CMake as `decode_dump` (with
`BUILD_TESTING`), it also runs a CTest on a few known instructions.

```bash
python ../athrill-arm-common/tools/crosscheck_t32_decoder32.py make aarchmrs/Instructions.json w32
build/decode_dump < w32/words.txt > w32/ours.txt
docker run --rm -v "$PWD/w32:/w" -w /w athrill-stm32-toolchain sh -c 'arm-none-eabi-objdump -D -b binary -m arm -M force-thumb all32.bin > gnu32.txt && arm-none-eabi-objcopy -I binary -O elf32-littlearm -B arm --rename-section .data=.text,contents,alloc,load,readonly,code all32.bin all32.o && llvm-objdump -d -z --triple=thumbv7em-none-eabi --mcpu=cortex-m7 all32.o > llvm32.txt'
python ../athrill-arm-common/tools/crosscheck_t32_decoder32.py check armv7m aarchmrs/Instructions.json w32
```

A sample that does not agree is put in a class that explains it, using Arm's
data: what the A-profile tree decodes it to, and whether its should-be bits
and operands are valid in Armv7-M. The check lists any sample left
unexplained. Result with the package above:

| Disassembler | Agree | Differ, explained | Unexplained |
|---|---|---|---|
| GNU objdump (arm-none-eabi 13.2) | 19,048 | 17,560 | 0 |
| llvm-objdump (Cortex-M7) | 29,181 | 7,427 | 0 |

GNU objdump disassembles for every Arm architecture at once, so most of its
differences are instructions Cortex-M7 does not have. The classes:

| Class | GNU | LLVM | Decoder | Why |
|---|---|---|---|---|
| Not in Armv7-M | 8,617 | 5 | undefined | A-profile, Advanced SIMD and Armv8 instructions |
| Coprocessor instructions (CDP, LDC, STC, MCR, MRC, MCRR, MRRC) | 3,746 | 5,542 | undefined | Armv7-M has them, but Cortex-M7 has no coprocessor besides the FPU, so they raise a UsageFault (NOCP) |
| Not decoded by Arm's A-profile data | 2,977 | 23 | undefined | Other architectures' instructions (FPA, Maverick, Armv8.1-M) and GNU's `??` forms; STRD with Rn = PC and writeback (UNPREDICTABLE) |
| Should-be bits that do not hold | 2,118 | 1,009 | the instruction | UNPREDICTABLE encodings; the data decodes them |
| D16-D31 or a register list past D15 | 0 | 714 | the instruction | Cortex-M7 has D0-D15; the executor must reject them |
| PC or SP as an operand | 16 | 66 | the instruction | UNPREDICTABLE encodings |
| Executed as another instruction | 82 | 68 | NOP, DSB, MRS, MSR | Unallocated hints, DSB options and reserved SYSm values, which the disassemblers name |
| LDRBT, LDRT, LDRSHT with Rn = PC | 4 | 0 | the literal load | Armv7-M decodes them as the literal load |

So the executors must raise a UsageFault for an undefined id: NOCP in the
coprocessor space, UNDEFINSTR elsewhere, including double-precision
registers above D15. They do: the 32-bit executors (`thumb_exec32.c`,
`thumb_gen32.c`) and the exception model are in place and tested against
QEMU ([testing](testing.md)).

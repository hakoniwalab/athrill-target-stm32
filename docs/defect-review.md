# Defect review: why it got in, why it was missed

When a piece of work turns up defects, fixing each one is not the end. For every defect we ask two questions,
and we review the whole code base for each answer:

- **Why did it get in?** The cause that produced the defect. Review: where else could the same cause have produced
  one?
- **Why was it not detected?** The gap in the checks that let it through. Review: what else could pass through the
  same gap?

A review that finds nothing is a valid result, as long as its evidence is written down.

## Steps

1. **List the defects** with their fixes (issue or PR), including the ones that turned out not to be defects and why.
2. **Group them by cause.** Several symptoms often share one cause. Each group is one line of review.
3. **Write both answers for each group**, in plain terms (what was assumed, what was not checked), not as blame.
4. **Open one review issue per cause and per side** (got in / missed), and one tracking issue that links them all
   and lists the defects. Each issue says:
   - why (the defect, the cause, how it got in or through);
   - the task: the scope (which files, models, boards, examples) and what to compare against (reference manual,
     driver source, another board);
   - **done when**: a table of what was checked with a verdict per row, and "nothing found" accepted with that table
     as evidence;
   - the owner.
5. **Share the reviews.** A review that is reading and tabulating, with a clear scope, can go to Codex as a
   read-only task (the issue text plus the local paths of the sources it needs). Codex writes its report; Claude
   checks it before it is posted. Reviews that need judgement about the design go to Claude.
   Check every citation in a report against the source itself: a section number that reads as evidence may not
   exist. Ask for register and bit names, which can be searched, rather than section numbers.
6. **Fix in separate issues and PRs.** A review does not change code. Each gap it finds becomes its own fix. When a
   fix changes a regression reference, the reference is re-recorded with the reason in the file.
7. **Close the tracking issue** when every review has a result.

## Example: 2026-11-14

Bringing DShot to the Pixhawk 4 and the Pixhawk 6C turned up eleven problems. Tracking issue: #93.

| Cause | Why it got in | Why it was missed |
| --- | --- | --- |
| Device models written from one firmware's use (DMA HTIF for a one-item transfer; H7 SDMMC started by DTEN) | Specs taken from what PX4/NuttX does, not from the manual's modes and boundary counts | Tests come from the specs; ArduPilot on the 6C was in no check |
| Board description gaps (6C SD card D0; no CAN2 node) | No checklist of the Athrill properties per peripheral; upstream SoC nodes taken as complete | The regression checks identity, and a deterministic 260 ms wait is identical every time |
| Register block without a handshake (bxCAN INAK) | Register blocks are for polled peripherals; the request/acknowledge pair was not checked | The driver's error was recorded in the reference console as expected output |
| Lost in the Devicetree migration (ArduCopter 6C example, its 400 MHz clock) | C-path-only examples removed without porting; the fix lived only in their configuration | No before/after inventory; README rows not tied to a check |
| Example settings not carried over (first boot location, airframe name) | A fix in one example, no propagation | The 6C examples checked arming, not hover altitude |
| Reference firmware not published | Reference recorded with a local build | A different firmware made the comparison SKIP, not fail |

The review of the errors recorded in the references (athrill-device#109) found two further gaps that none of the
eleven problems showed: the Pixhawk 4 board had no BMI055, and the 6C board had no 24LC64 EEPROM.

What the reviews found, and what became of it:

| Review | Found | Fix |
| --- | --- | --- |
| Model specs against the manuals (athrill-device#106) | I2C RELOAD continuation, H7 SPI end of transfer, DMA error flags, SDMMC errors and CMD12; wrong chapter numbers | athrill-device#111–#114, #119, all merged; reproduced first by tests that failed |
| Board Devicetree differences (athrill-device#107) | nothing new | — |
| Register blocks the firmware waits on (athrill-device#108) | FDCAN without completion, ADC reading 0 V | deferred (#117, #118): the flights do not need them |
| Errors in the references (athrill-device#109) | Pixhawk 4 BMI055, 6C 24LC64 | deferred (#115, #116) |
| What the migration removed (#89) | ArduCopter 6C examples; three unit tests | #98 (examples back, merged); tests ported |
| Settings across examples (#90) | two parameters PX4 v1.17.0 does not have; a bench script without its board | #94, #95, merged |
| Coverage and README claims (#91) | stale 6C rows; ArduCopter 6C slower | #96, #97 (two hot-path lookups removed; tables measured again) |
| Checks that pass deterministic failures (#92) | no console check; SKIP passing; a check that ignored its reference; CI ignoring a failure before `tee` | console allow-lists and strict mode, merged |

The console check found one more defect on its first run: the parameter script set two names that do not exist
in PX4 v1.17.0, so they had never taken effect (#94).


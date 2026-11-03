/* QEMU's mps2-an500 for the floating-point tests: code from 0x00000000
 * (SSRAM1), data at 0x20000000 as on the STM32 (link.ld). */
MEMORY
{
    CODE  (rx)  : ORIGIN = 0x00000000, LENGTH = 4096K
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 4096K
}
_estack = ORIGIN(SRAM) + 0x1000;
ENTRY(reset_handler)
SECTIONS
{
    .text : { KEEP(*(.vectors)) *(.text*) *(.rodata*) } > CODE
}

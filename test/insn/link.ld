/* STM32F765: 2 MiB flash at 0x08000000, 512 KiB SRAM at 0x20000000. */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 512K
}
/* The stack starts low in SRAM, so that the LDR (SP) test can read above it. */
_estack = ORIGIN(SRAM) + 0x1000;
ENTRY(reset_handler)
SECTIONS
{
    .text : { KEEP(*(.vectors)) *(.text*) *(.rodata*) } > FLASH
}

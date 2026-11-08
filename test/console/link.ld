/* STM32F765: 2 MiB flash at 0x08000000, 512 KiB SRAM at 0x20000000. */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 2048K
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 512K
}
_estack = ORIGIN(SRAM) + LENGTH(SRAM);
ENTRY(reset_handler)
SECTIONS
{
    .text : { KEEP(*(.vectors)) *(.text*) *(.rodata*) } > FLASH
}

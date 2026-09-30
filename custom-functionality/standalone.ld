/* Preserve the known application RAM layout and data-table addresses while
 * allowing normal symbol-based linking for every executable function. */
ENTRY(Reset_Handler)
MEMORY {
    FLASH (rx) : ORIGIN = DEFINED(OTA_APP) ? 0x08002000 : 0x08000000, LENGTH = DEFINED(OTA_APP) ? 120K : 256K
    APP (rw) : ORIGIN = 0x20000000, LENGTH = 0x700
    RAM (rwx) : ORIGIN = 0x20000700, LENGTH = 0xf900
}
_stack_top = 0x20010000;
SECTIONS {
    .vectors ORIGIN(FLASH) : { KEEP(*(.vectors)) } > FLASH
    .text : { *(.text*) *(.rodata*) } > FLASH
    .ARM.exidx : { *(.ARM.exidx*) } > FLASH
    ASSERT(. <= 0x08010b74, "Executable code overlaps recovered flash tables")
    .sh03_tables 0x08010b74 : { KEEP(*(.sh03_tables)) } > FLASH
    .data : { _data_start = .; *(.data*) _data_end = .; } > RAM AT> FLASH
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(COMMON) . = ALIGN(8); _bss_end = .; } > RAM
    ASSERT(_bss_end <= _stack_top - 0x1000, "Insufficient main/IRQ stack space")
    /DISCARD/ : { *(.comment) *(.note*) }
}

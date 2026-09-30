ENTRY(Boot_Reset)
MEMORY {
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 8K
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}
_stack_top = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS {
    .vectors : { KEEP(*(.vectors)) } > FLASH
    .text : { *(.text*) *(.rodata*) } > FLASH
    .ARM.exidx : { *(.ARM.exidx*) } > FLASH
    .data : { _data_start = .; *(.data*) _data_end = .; } > RAM AT> FLASH
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(COMMON) . = ALIGN(8); _bss_end = .; } > RAM
    ASSERT(_bss_end < _stack_top - 4096, "Bootloader stack overflow")
    /DISCARD/ : { *(.comment) *(.note*) }
}

OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(vmlinuz_entry)
SECTIONS
{
    . = 0x44200000;
    .setup : { *(.setup*) }
    . = 0x44201000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}

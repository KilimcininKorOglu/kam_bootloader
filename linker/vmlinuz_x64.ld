OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(vmlinuz_entry)
SECTIONS
{
    . = 0x2000000;
    .setup : { *(.setup*) }
    . = 0x2001000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}

OUTPUT_FORMAT("pei-aarch64-little")
ENTRY(efi_main)
SECTIONS
{
    . = 0;
    .text : { *(.text*) }
    .data : { *(.data* .rodata*) }
    .reloc : { *(.reloc*) }
}

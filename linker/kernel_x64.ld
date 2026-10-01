OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(kam_kernel_main)
SECTIONS
{
    . = 0x100000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON*) }
    /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}

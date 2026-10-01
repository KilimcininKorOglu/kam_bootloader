OUTPUT_FORMAT("elf64-littleaarch64")
OUTPUT_ARCH(aarch64)
ENTRY(kam_kernel_main)
/* Fixed at 0x42000000: inside QEMU virt DRAM (starts at 0x40000000).
 * Real hardware puts DRAM elsewhere; a PIE kernel (Phase 5) removes this. */
SECTIONS
{
    . = 0x42000000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON*) }
    /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}

#ifndef KAM_BZIMAGE_H
#define KAM_BZIMAGE_H

/* x86 Linux boot protocol, loader side. Offsets are boot_params byte
 * offsets (aka setup_header offsets). Handover/e820 numbers follow
 * boot.txt; the crafted fixture pins them down in QEMU.
 *
 * Fixed load bases mirror the kernel.elf approach (PIE removes them later):
 * must be free RAM on the target (aa64 virt DRAM starts at 0x40000000). */

#include "types.h"

#define KAM_BZ_MAGIC_AA55 0xAA55u
#define KAM_BZ_HDRS_MAGIC 0x53726448u /* "HdrS" */
#define KAM_BZ_VERSION_MIN 0x020Cu    /* EFI handover present */

#define KAM_BZ_OFF_HDRS 0x202u
#define KAM_BZ_OFF_VERSION 0x206u
#define KAM_BZ_OFF_LOADER_TYPE 0x210u
#define KAM_BZ_OFF_HANDOVER 0x264u
#define KAM_BZ_OFF_RAMDISK_IMG 0x218u
#define KAM_BZ_OFF_RAMDISK_SIZE 0x21Cu
#define KAM_BZ_OFF_CMDLINE 0x228u
#define KAM_BZ_OFF_E820_COUNT 0x1E8u
#define KAM_BZ_OFF_E820_MAP 0x2D0u
#define KAM_BZ_E820_ENTRY_SIZE 20u
#define KAM_BZ_E820_MAX 128u

#if defined(__x86_64__)
#define KAM_BZ_BASE 0x2000000u
#define KAM_BZ_INITRD_BASE 0x3000000u
/* Params address mailbox for the handover jump (low RAM, ours). */
#define KAM_BZ_MBOX 0x6000u
#elif defined(__aarch64__)
#define KAM_BZ_BASE 0x44200000u
#define KAM_BZ_INITRD_BASE 0x45000000u
/* Params address mailbox: free conventional page on virt. */
#define KAM_BZ_MBOX 0x441B4000u
#else
#define KAM_BZ_BASE 0x2000000u
#define KAM_BZ_INITRD_BASE 0x3000000u
#define KAM_BZ_MBOX 0x6000u
#endif

#endif

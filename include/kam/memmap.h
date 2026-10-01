#ifndef KAM_MEMMAP_H
#define KAM_MEMMAP_H

/* Shared memory-map format. Shaped like a BIOS E820 entry so the BIOS
 * path can store firmware entries verbatim; the UEFI path converts
 * EFI_MEMORY_DESCRIPTORs into the same shape. */

#include "types.h"

typedef struct kam_mementry {
    kam_u64 base;
    kam_u64 len;
    kam_u32 type;   /* 1 = free RAM (E820 + UEFI ConventionalMemory agree) */
    kam_u32 flags;  /* E820 extended attributes / ACPI bits */
} kam_mementry_t;

#define KAM_MEMMAP_MAX 64

typedef struct kam_memmap {
    kam_u32 count;
    kam_u32 _pad;
    kam_mementry_t entries[KAM_MEMMAP_MAX];
} kam_memmap_t;

KAM_STATIC_ASSERT(sizeof(kam_mementry_t) == 24, mementry_size);

#endif

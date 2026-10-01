#ifndef KAM_ELF_H
#define KAM_ELF_H

/* Minimal ELF64 loader shared by the BIOS and UEFI paths.
 * prepare() validates and describes PT_LOAD segments; the caller owns
 * the destination pages (fixed layout on BIOS, AllocatePages on UEFI);
 * commit() copies file bytes and zeroes the BSS tail. */

#include "types.h"

#define KAM_ELF_MAGIC0 0x7Fu
#define KAM_ELF_MAGIC1 'E'
#define KAM_ELF_MAGIC2 'L'
#define KAM_ELF_MAGIC3 'F'
#define KAM_ELF_CLASS64 2u
#define KAM_ELF_DATA_LE 1u
#define KAM_ELF_TYPE_EXEC 2u
#define KAM_ELF_PT_LOAD 1u
#define KAM_ELF_MAXSEG 8u

typedef struct kam_elf_hdr {
    kam_u8 ident[16];
    kam_u16 type;
    kam_u16 machine;
    kam_u32 version;
    kam_u64 entry;
    kam_u64 phoff;
    kam_u64 shoff;
    kam_u32 flags;
    kam_u16 ehsize;
    kam_u16 phentsize;
    kam_u16 phnum;
    kam_u16 shentsize;
    kam_u16 shnum;
    kam_u16 shstrndx;
} kam_elf_hdr_t;

typedef struct kam_elf_phdr {
    kam_u32 type;
    kam_u32 flags;
    kam_u64 offset;
    kam_u64 vaddr;
    kam_u64 paddr;
    kam_u64 filesz;
    kam_u64 memsz;
    kam_u64 align;
} kam_elf_phdr_t;

typedef struct kam_seg {
    kam_u64 paddr;
    kam_u64 filesz;
    kam_u64 memsz;
    kam_u64 offset;
} kam_seg_t;

KAM_STATIC_ASSERT(sizeof(kam_elf_hdr_t) == 64, elf_hdr_size);
KAM_STATIC_ASSERT(sizeof(kam_elf_phdr_t) == 56, elf_phdr_size);

/* Validate image, fill segs, return entry point (0 = invalid). */
static inline kam_u64 kam_elf_prepare(const kam_u8 *img, kam_usize img_size,
                                      kam_seg_t *segs, kam_usize *nseg_out) {
    const kam_elf_hdr_t *h = (const kam_elf_hdr_t *)img;
    kam_usize i, n = 0;

    if (img_size < sizeof(kam_elf_hdr_t))
        return 0;
    if (h->ident[0] != KAM_ELF_MAGIC0 || h->ident[1] != KAM_ELF_MAGIC1 ||
        h->ident[2] != KAM_ELF_MAGIC2 || h->ident[3] != KAM_ELF_MAGIC3)
        return 0;
    if (h->ident[4] != KAM_ELF_CLASS64 || h->ident[5] != KAM_ELF_DATA_LE)
        return 0;
    if (h->type != KAM_ELF_TYPE_EXEC || h->phentsize != sizeof(kam_elf_phdr_t))
        return 0;
    if (h->phoff + (kam_u64)h->phnum * sizeof(kam_elf_phdr_t) > img_size)
        return 0;

    for (i = 0; i < h->phnum; i++) {
        const kam_elf_phdr_t *p =
            (const kam_elf_phdr_t *)(img + h->phoff + i * sizeof(kam_elf_phdr_t));
        if (p->type != KAM_ELF_PT_LOAD)
            continue;
        if (n >= KAM_ELF_MAXSEG || p->filesz > p->memsz)
            return 0;
        if (p->offset + p->filesz > img_size || p->paddr == 0)
            return 0;
        segs[n].paddr = p->paddr;
        segs[n].filesz = p->filesz;
        segs[n].memsz = p->memsz;
        segs[n].offset = p->offset;
        n++;
    }
    if (n == 0 || h->entry == 0)
        return 0;
    *nseg_out = n;
    return h->entry;
}

/* Copy file bytes to their physical addresses, zero the BSS tail. */
static inline void kam_elf_commit(const kam_u8 *img, const kam_seg_t *segs,
                                  kam_usize nseg) {
    kam_usize i, j;
    for (i = 0; i < nseg; i++) {
        kam_u8 *dst = (kam_u8 *)segs[i].paddr;
        for (j = 0; j < segs[i].filesz; j++)
            dst[j] = img[segs[i].offset + j];
        for (j = segs[i].filesz; j < segs[i].memsz; j++)
            dst[j] = 0;
    }
}

#endif

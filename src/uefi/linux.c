/* KAM direct bzImage boot. Claim-first ordering: fixed regions are
 * reserved before pool reads, so AllocatePool cannot squat on them.
 * Params page carries e820 + cmdline + ramdisk at genuine offsets. */

#include "kam/linux.h"
#include "kam/efi.h"
#include "kam/memmap.h"
#include "kam/bzimage.h"

typedef void (*kam_bz_entry_fn)(const kam_u8 *params);

kam_usize kam_bz_last_status;
kam_u64 kam_bz_last_want;

static kam_u16 kam_rd16(const kam_u8 *p) {
    return (kam_u16)((kam_u16)p[0] | ((kam_u16)p[1] << 8));
}

static kam_u32 kam_rd32(const kam_u8 *p) {
    return (kam_u32)p[0] | ((kam_u32)p[1] << 8) | ((kam_u32)p[2] << 16) |
           ((kam_u32)p[3] << 24);
}

static void kam_wr32(kam_u8 *p, kam_u32 v) {
    p[0] = (kam_u8)(v & 0xFF);
    p[1] = (kam_u8)((v >> 8) & 0xFF);
    p[2] = (kam_u8)((v >> 16) & 0xFF);
    p[3] = (kam_u8)((v >> 24) & 0xFF);
}

int kam_bz_claim(struct kam_boot_services *bs, kam_u64 *img_addr,
                 kam_u64 *ird_addr, kam_u64 *params_addr) {
    kam_u64 a, b, c;
    kam_status_t s;

    if (!bs)
        return -1;
    a = KAM_BZ_BASE;
    s = bs->alloc_pages(KAM_ALLOC_ADDRESS, KAM_EFI_LOADER_DATA,
                        KAM_BZ_RESERVE_PAGES, &a);
    if (KAM_EFI_ERROR(s) || a != KAM_BZ_BASE) {
        kam_bz_last_status = (kam_usize)s;
        kam_bz_last_want = KAM_BZ_BASE;
        return -2;
    }
    b = KAM_BZ_INITRD_BASE;
    s = bs->alloc_pages(KAM_ALLOC_ADDRESS, KAM_EFI_LOADER_DATA,
                        KAM_BZ_RESERVE_PAGES, &b);
    if (KAM_EFI_ERROR(s) || b != KAM_BZ_INITRD_BASE) {
        kam_bz_last_status = (kam_usize)s;
        kam_bz_last_want = KAM_BZ_INITRD_BASE;
        return -3;
    }
    c = 0;
    s = bs->alloc_pages(0, KAM_EFI_LOADER_DATA, 2, &c);
    if (KAM_EFI_ERROR(s) || c == 0 || c + 8192u < c)
        return -4;
    *img_addr = a;
    *ird_addr = b;
    *params_addr = c;
    return 1;
}

int kam_bz_boot(struct kam_boot_services *bs, const kam_u8 *img,
                kam_usize img_size, const kam_u8 *initrd,
                kam_usize initrd_size, const char *cmdline,
                const struct kam_memmap *map, kam_u64 img_addr,
                kam_u64 ird_addr, kam_u64 params_addr) {
    kam_u32 ho, i;
    kam_u8 *params;
    kam_u8 *cmd;
    kam_usize clen = 0;
    (void)bs;

    if (!img || img_size < 512 || !map || img_addr == 0 || params_addr == 0)
        return -5;
    if (img_size > (kam_usize)KAM_BZ_RESERVE_PAGES * 4096u)
        return -5;
    if (kam_rd16(img + 510) != KAM_BZ_MAGIC_AA55)
        return -6;
    if (kam_rd32(img + KAM_BZ_OFF_HDRS) != KAM_BZ_HDRS_MAGIC)
        return -7;
    if (kam_rd16(img + KAM_BZ_OFF_VERSION) < KAM_BZ_VERSION_MIN)
        return -8;
    ho = kam_rd32(img + KAM_BZ_OFF_HANDOVER);
    if (ho == 0 || ho >= img_size)
        return -9;
    if (initrd && initrd_size > (kam_usize)KAM_BZ_RESERVE_PAGES * 4096u)
        return -10;

    for (i = 0; i < img_size; i++)
        ((kam_u8 *)img_addr)[i] = img[i];
    if (initrd && initrd_size > 0) {
        if (ird_addr == 0)
            return -10;
        for (i = 0; i < initrd_size; i++)
            ((kam_u8 *)ird_addr)[i] = initrd[i];
    }

    params = (kam_u8 *)params_addr;
    for (i = 0; i < 8192; i++)
        params[i] = 0;
    while (cmdline && cmdline[clen] && clen < 255)
        clen++;
    cmd = params + 4096;
    for (i = 0; i < clen; i++)
        cmd[i] = (kam_u8)cmdline[i];
    cmd[clen] = 0;
    if (params_addr + 4096u > 0xFFFFFFFFu)
        return -11;
    params[KAM_BZ_OFF_LOADER_TYPE] = 0xE0;
    kam_wr32(params + KAM_BZ_OFF_CMDLINE, (kam_u32)(params_addr + 4096u));
    if (initrd && initrd_size > 0) {
        if (ird_addr > 0xFFFFFFFFu)
            return -12;
        kam_wr32(params + KAM_BZ_OFF_RAMDISK_IMG, (kam_u32)ird_addr);
        kam_wr32(params + KAM_BZ_OFF_RAMDISK_SIZE, (kam_u32)initrd_size);
    }

    /* e820 from the shared map: usable -> 1, everything else -> 2. */
    {
        kam_u32 n = map->count;
        kam_u32 k;
        if (n > KAM_BZ_E820_MAX)
            n = KAM_BZ_E820_MAX;
        params[KAM_BZ_OFF_E820_COUNT] = (kam_u8)n;
        for (k = 0; k < n; k++) {
            kam_u8 *e = params + KAM_BZ_OFF_E820_MAP +
                        (kam_usize)k * KAM_BZ_E820_ENTRY_SIZE;
            kam_u64 adr = map->entries[k].base;
            kam_u64 len = map->entries[k].len;
            kam_u32 t = (map->entries[k].type == 1) ? 1u : 2u;
            kam_usize j;
            for (j = 0; j < 8; j++) {
                e[j] = (kam_u8)((adr >> (j * 8)) & 0xFF);
                e[8 + j] = (kam_u8)((len >> (j * 8)) & 0xFF);
            }
            kam_wr32(e + 16, t);
        }
    }

    /* Publish the params address through the mailbox, then jump. */
    *(volatile kam_u64 *)(kam_usize)KAM_BZ_MBOX = params_addr;
#if defined(__x86_64__)
    __asm__ volatile("mov %0, %%rdi\n\t"
                     "mov %1, %%rsi\n\t"
                     "call *%2"
                     :
                     : "r"((kam_u64)0),
                       "r"((kam_u64)(kam_usize)params),
                       "r"((kam_u64)(img_addr + ho))
                     : "memory", "cc", "rax", "rcx", "rdx", "rsi", "rdi",
                       "r8", "r9", "r10", "r11");
#else
    ((kam_bz_entry_fn)(kam_usize)(img_addr + ho))(params);
#endif
    return 1;
}

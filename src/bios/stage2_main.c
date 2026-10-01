/* KAM BIOS stage2 C payload. Runs in 64-bit long mode, entered from
 * stage2.asm. Freestanding, BSS-free by construction (fixed-address
 * concat has no loader to zero BSS, so all state is stack or assigned). */

#include "kam/types.h"
#include "kam/memmap.h"
#include "kam/bios_addrs.h"
#include "kam/bios_console.h"
#include "kam/elf.h"

typedef void (*kam_kernel_fn)(const kam_memmap_t *map);

void kam_bios_main(void) {
    const volatile kam_memmap_t *map =
        (const volatile kam_memmap_t *)KAM_E820_BASE;
    kam_u32 i;
    kam_u64 free_bytes = 0;
    kam_u32 count;
    kam_seg_t segs[KAM_ELF_MAXSEG];
    kam_usize nseg = 0;
    kam_u64 entry;

    kam_cons_init();
    kam_puts("KAM BIOS stage2\n");

    count = map->count;
    if (count > KAM_MEMMAP_MAX)
        count = KAM_MEMMAP_MAX;
    kam_puts("E820 entries: ");
    kam_put_u64(count);
    kam_putc('\n');

    for (i = 0; i < count; i++) {
        if (map->entries[i].type == 1)
            free_bytes += map->entries[i].len;
    }
    kam_puts("Free RAM: ");
    kam_put_u64(free_bytes);
    kam_puts(" bytes\n");

    /* The MBR staged KERNEL.ELF as raw file bytes; parse + load it. */
    entry = kam_elf_prepare((const kam_u8 *)KAM_KFILE_BASE, KAM_KFILE_MAX,
                            segs, &nseg);
    if (entry == 0) {
        kam_puts("KAM: bad kernel ELF\n");
        return;
    }
    kam_puts("KAM: kernel segs: ");
    kam_put_u64(nseg);
    kam_putc('\n');
    kam_puts("KAM: entry ");
    kam_put_hex(entry);
    kam_puts(" seg0 ");
    kam_put_hex(segs[0].paddr);
    kam_putc('\n');
    kam_elf_commit((const kam_u8 *)KAM_KFILE_BASE, segs, nseg);
    kam_puts("KAM: jumping to kernel.\n");
    ((kam_kernel_fn)entry)((const kam_memmap_t *)KAM_E820_BASE);
    kam_puts("KAM: kernel returned, halting.\n");
}

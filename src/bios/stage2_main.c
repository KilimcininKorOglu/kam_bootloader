/* KAM BIOS stage2 C payload. Runs in 64-bit long mode, entered from
 * stage2.asm. Freestanding, BSS-free by construction (fixed-address
 * concat has no loader to zero BSS, so all state is stack or assigned). */

#include "kam/types.h"
#include "kam/memmap.h"
#include "kam/bios_addrs.h"
#include "kam/bios_console.h"

void kam_bios_main(void) {
    const volatile kam_memmap_t *map =
        (const volatile kam_memmap_t *)KAM_E820_BASE;
    kam_u32 i;
    kam_u64 free_bytes = 0;
    kam_u32 count;

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
    kam_puts("KAM: payload done, halting.\n");
}

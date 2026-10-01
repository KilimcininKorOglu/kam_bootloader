/* KAM test kernel. Loaded by both the BIOS and UEFI paths as an ELF64
 * image linked at 1MB; entered as kam_kernel_main(map) in 64-bit mode.
 * Uses only the bare-metal serial (no firmware on either path). */

#include "kam/types.h"
#include "kam/memmap.h"
#include "kam/raw_serial.h"

void kam_kernel_main(const kam_memmap_t *map) {
    kam_u32 i, count;
    kam_u64 free_bytes = 0;

    kam_raw_serial_init();
    kam_raw_puts("KAM-KERNEL\n");

    count = map->count;
    if (count > KAM_MEMMAP_MAX)
        count = KAM_MEMMAP_MAX;
    kam_raw_puts("Kernel sees entries: ");
    kam_raw_put_u64(count);
    kam_raw_puts("\n");

    for (i = 0; i < count; i++) {
        if (map->entries[i].type == 1)
            free_bytes += map->entries[i].len;
    }
    kam_raw_puts("Kernel free RAM: ");
    kam_raw_put_u64(free_bytes);
    kam_raw_puts(" bytes\n");
    kam_raw_puts("KAM-KERNEL halting.\n");
    kam_raw_halt();
}

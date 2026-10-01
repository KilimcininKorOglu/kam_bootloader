/* KAM-HELLO3: data-partition chainload fixture. Lives on partition 2;
 * proves entries boot from non-ESP partitions of the same disk. */

#include "kam/efi.h"
#include "kam/console.h"

kam_status_t hello3_main(kam_handle_t image, kam_system_table_t *st) {
    (void)image;
    if (!st || !st->con_out)
        return KAM_EFI_UNSUPPORTED;
    kam_puts(st, "KAM-HELLO3\n");
    return KAM_EFI_SUCCESS;
}

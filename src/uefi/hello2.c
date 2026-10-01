/* KAM-HELLO2: second-disk chainload fixture. Lives on the auxiliary
 * test volume; proves entries boot from volumes other than our own. */

#include "kam/efi.h"
#include "kam/console.h"

kam_status_t hello2_main(kam_handle_t image, kam_system_table_t *st) {
    (void)image;
    if (!st || !st->con_out)
        return KAM_EFI_UNSUPPORTED;
    kam_puts(st, "KAM-HELLO2\n");
    return KAM_EFI_SUCCESS;
}

/* KAM-HELLO: chainload fixture. Loaded via LoadImage/StartImage by the
 * main loader; proves third-party EFI chaining works. Stays in boot
 * services, prints, returns. */

#include "kam/efi.h"
#include "kam/console.h"

kam_status_t hello_main(kam_handle_t image, kam_system_table_t *st) {
    (void)image;
    if (!st || !st->con_out)
        return KAM_EFI_UNSUPPORTED;
    kam_puts(st, "KAM-HELLO\n");
    return KAM_EFI_SUCCESS;
}

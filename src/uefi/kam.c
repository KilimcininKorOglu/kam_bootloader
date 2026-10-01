/* KAM UEFI application. x86_64 + AArch64 from a single source.
 * Freestanding C, no libc. */

#include "kam/efi.h"
#include "kam/console.h"

#if defined(__x86_64__)
#define KAM_ARCH_NAME "x86_64 UEFI"
#elif defined(__aarch64__)
#define KAM_ARCH_NAME "AArch64 UEFI"
#else
#define KAM_ARCH_NAME "unknown UEFI"
#endif

/* UEFI entry point: the linker script makes this symbol the entry. */
kam_status_t efi_main(kam_handle_t image, kam_system_table_t *st) {
    (void)image;

    if (!st || !st->con_out)
        return KAM_EFI_UNSUPPORTED;

    /* No screen clear: ClearScreen is optional on some firmware. */
    kam_puts(st, "KAM " KAM_ARCH_NAME "\n");
    kam_puts(st, "KAM: returning for testing, ExitBootServices not wired up yet.\n");

    return KAM_EFI_SUCCESS;
}

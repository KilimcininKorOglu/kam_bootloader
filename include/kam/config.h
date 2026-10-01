#ifndef KAM_CONFIG_H
#define KAM_CONFIG_H

/* KAM.INI static boot configuration. Line-based, freestanding, no alloc:
 *
 *   timeout 5      # seconds, 0 = boot default immediately
 *   default 1      # 1-based entry number
 *
 *   [kernel]
 *   label My Kernel
 *   path \KAM\KERNEL.ELF
 *
 *   [chain]        # LoadImage/StartImage
 *   label Hello
 *   path \KAM\HELLO.EFI
 *
 *   [iso]          # ISO9660 + El Torito probe
 *   label Test ISO
 *   path \KAM\TEST.ISO
 *
 *   [linux]        # direct bzImage boot
 *   label Test Linux
 *   path \KAM\VMLINUZ
 *   initrd \KAM\INITRD.IMG
 *   cmdline kam-test console=ttyS0
 *
 *   password_hash <64 hex>  # SHA-256(salt + password), gates booting
 *   password_salt <text>
 *
 * Unknown sections/keys are ignored. Entries without a path are dropped.
 * Callers merge config entries first, then append scan results
 * (dedupe by path). */

#include "types.h"
#include "scan.h"

#define KAM_SALT_CHARS 64u

typedef struct kam_config {
    kam_entry_t entries[KAM_SCAN_MAX];
    kam_usize count;
    kam_usize timeout;
    kam_usize def;
    int has_pw;
    kam_u8 pw_hash[32];
    char pw_salt[KAM_SALT_CHARS];
} kam_config_t;

void kam_config_parse(const kam_u8 *buf, kam_usize size, kam_config_t *cfg);

#endif

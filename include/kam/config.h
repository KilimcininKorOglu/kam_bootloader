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
 * Unknown sections/keys are ignored. Entries without a path are dropped.
 * Callers merge config entries first, then append scan results
 * (dedupe by path). */

#include "types.h"
#include "scan.h"

kam_usize kam_config_parse(const kam_u8 *buf, kam_usize size,
                           kam_entry_t *out, kam_usize max,
                           kam_usize *timeout_out, kam_usize *def_out);

#endif

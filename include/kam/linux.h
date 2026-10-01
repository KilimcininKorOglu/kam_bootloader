#ifndef KAM_LINUX_H
#define KAM_LINUX_H

/* Direct bzImage boot, two phases so fixed regions are claimed before any
 * pool allocation can squat on them:
 *   kam_bz_claim: reserve image/initrd/params regions (negative on failure)
 *   kam_bz_boot: validate, copy, fill params, jump (never returns)
 */

#include "types.h"

#define KAM_BZ_RESERVE_PAGES 16u

struct kam_boot_services;
struct kam_memmap;

int kam_bz_claim(struct kam_boot_services *bs, kam_u64 *img_addr,
                 kam_u64 *ird_addr, kam_u64 *params_addr);
int kam_bz_boot(struct kam_boot_services *bs, const kam_u8 *img,
                kam_usize img_size, const kam_u8 *initrd,
                kam_usize initrd_size, const char *cmdline,
                const struct kam_memmap *map, kam_u64 img_addr,
                kam_u64 ird_addr, kam_u64 params_addr);

/* Last EFI status seen by claim/boot (0 = logic error instead). */
extern kam_usize kam_bz_last_status;
extern kam_u64 kam_bz_last_want;

#endif

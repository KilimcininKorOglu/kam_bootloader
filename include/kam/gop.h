#ifndef KAM_GOP_H
#define KAM_GOP_H

/* GOP header art: dark background, orange top bar, white frame and three
 * signal bars. Returns 1 when something was drawn (w/h filled in). */

#include "types.h"

struct kam_boot_services;
struct kam_system_table;

int kam_gop_draw(struct kam_boot_services *bs, kam_u32 *w_out,
                 kam_u32 *h_out);

#endif

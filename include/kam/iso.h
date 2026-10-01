#ifndef KAM_ISO_H
#define KAM_ISO_H

/* Read-only ISO9660 + El Torito probe. Parses a resident image (already
 * read into RAM), prints the volume layout through caller callbacks,
 * returns 0 when the test image matches every expectation. */

#include "types.h"

typedef void (*kam_log_puts)(const char *s);
typedef void (*kam_log_putc)(char c);
typedef void (*kam_log_u64)(kam_u64 v);

int kam_iso_boot_report(const kam_u8 *img, kam_usize size,
                        kam_log_puts puts, kam_log_putc putc,
                        kam_log_u64 putu);

#endif

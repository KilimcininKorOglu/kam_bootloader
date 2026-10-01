#ifndef KAM_SHA256_H
#define KAM_SHA256_H

/* Freestanding SHA-256 (FIPS 180-4).
 * kam_sha256_once is all most callers need. */

#include "types.h"

typedef struct kam_sha256 {
    kam_u32 h[8];
    kam_u64 len;
    kam_u8 buf[64];
    kam_usize used;
} kam_sha256_t;

void kam_sha256_init(kam_sha256_t *c);
void kam_sha256_update(kam_sha256_t *c, const kam_u8 *data, kam_usize n);
void kam_sha256_final(kam_sha256_t *c, kam_u8 out[32]);

static inline void kam_sha256_once(const kam_u8 *data, kam_usize n,
                                   kam_u8 out[32]) {
    kam_sha256_t c;
    kam_sha256_init(&c);
    kam_sha256_update(&c, data, n);
    kam_sha256_final(&c, out);
}

/* Constant-time equality (no early exit). */
static inline int kam_ct_eq(const kam_u8 *a, const kam_u8 *b, kam_usize n) {
    kam_u8 d = 0;
    kam_usize i;
    for (i = 0; i < n; i++)
        d |= (kam_u8)(a[i] ^ b[i]);
    return d == 0;
}

#endif

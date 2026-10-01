/* KAM SHA-256. Textbook Merkle-Damgard, 32-bit words, big-endian
 * padding, following FIPS 180-4. */

#include "kam/sha256.h"

static const kam_u32 KAM_K[64] = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u, 0x3956C25Bu,
    0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u, 0xD807AA98u, 0x12835B01u,
    0x243185BEu, 0x550C7DC3u, 0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u,
    0xC19BF174u, 0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu, 0x983E5152u,
    0xA831C66Du, 0xB00327C8u, 0xBF597FC7u, 0xC6E00BF3u, 0xD5A79147u,
    0x06CA6351u, 0x14292967u, 0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu,
    0x53380D13u, 0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u, 0xD192E819u,
    0xD6990624u, 0xF40E3585u, 0x106AA070u, 0x19A4C116u, 0x1E376C08u,
    0x2748774Cu, 0x34B0BCB5u, 0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu,
    0x682E6FF3u, 0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u};

static kam_u32 kam_rotr(kam_u32 v, kam_u32 n) {
    return (v >> n) | (v << (32u - n));
}

static void kam_block(kam_sha256_t *c, const kam_u8 *p) {
    kam_u32 w[64];
    kam_u32 v[8];
    kam_u32 t1, t2;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((kam_u32)p[i * 4] << 24) | ((kam_u32)p[i * 4 + 1] << 16) |
               ((kam_u32)p[i * 4 + 2] << 8) | (kam_u32)p[i * 4 + 3];
    }
    for (i = 16; i < 64; i++) {
        kam_u32 s0 = kam_rotr(w[i - 15], 7) ^ kam_rotr(w[i - 15], 18) ^
                     (w[i - 15] >> 3);
        kam_u32 s1 = kam_rotr(w[i - 2], 17) ^ kam_rotr(w[i - 2], 19) ^
                     (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    for (i = 0; i < 8; i++)
        v[i] = c->h[i];
    for (i = 0; i < 64; i++) {
        kam_u32 s1 = kam_rotr(v[4], 6) ^ kam_rotr(v[4], 11) ^
                     kam_rotr(v[4], 25);
        kam_u32 ch = (v[4] & v[5]) ^ ((~v[4]) & v[6]);
        kam_u32 s0 = kam_rotr(v[0], 2) ^ kam_rotr(v[0], 13) ^
                     kam_rotr(v[0], 22);
        kam_u32 maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        t1 = v[7] + s1 + ch + KAM_K[i] + w[i];
        t2 = s0 + maj;
        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + t1;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = t1 + t2;
    }
    for (i = 0; i < 8; i++)
        c->h[i] += v[i];
}

void kam_sha256_init(kam_sha256_t *c) {
    c->h[0] = 0x6A09E667u;
    c->h[1] = 0xBB67AE85u;
    c->h[2] = 0x3C6EF372u;
    c->h[3] = 0xA54FF53Au;
    c->h[4] = 0x510E527Fu;
    c->h[5] = 0x9B05688Cu;
    c->h[6] = 0x1F83D9ABu;
    c->h[7] = 0x5BE0CD19u;
    c->len = 0;
    c->used = 0;
}

void kam_sha256_update(kam_sha256_t *c, const kam_u8 *data, kam_usize n) {
    kam_usize i = 0;
    c->len += (kam_u64)n * 8u;
    while (i < n) {
        kam_usize room = 64u - c->used;
        kam_usize j = 0;
        if (room > n - i)
            room = n - i;
        while (j < room) {
            c->buf[c->used + j] = data[i + j];
            j++;
        }
        c->used += room;
        i += room;
        if (c->used == 64) {
            kam_block(c, c->buf);
            c->used = 0;
        }
    }
}

void kam_sha256_final(kam_sha256_t *c, kam_u8 out[32]) {
    kam_u8 pad[64];
    kam_usize i;
    for (i = 0; i < 64; i++)
        pad[i] = 0;
    pad[0] = 0x80;
    if (c->used < 56) {
        kam_usize k;
        for (k = 0; k < 56 - c->used; k++)
            c->buf[c->used + k] = pad[k];
        c->used = 56;
    } else {
        kam_usize k;
        for (k = 0; k < 64 - c->used; k++)
            c->buf[c->used + k] = pad[k];
        kam_block(c, c->buf);
        for (k = 0; k < 56; k++)
            c->buf[k] = 0;
        c->used = 56;
    }
    for (i = 0; i < 8; i++)
        c->buf[56 + i] = (kam_u8)((c->len >> (56 - i * 8)) & 0xFFu);
    kam_block(c, c->buf);
    for (i = 0; i < 8; i++) {
        out[i * 4] = (kam_u8)((c->h[i] >> 24) & 0xFFu);
        out[i * 4 + 1] = (kam_u8)((c->h[i] >> 16) & 0xFFu);
        out[i * 4 + 2] = (kam_u8)((c->h[i] >> 8) & 0xFFu);
        out[i * 4 + 3] = (kam_u8)(c->h[i] & 0xFFu);
    }
}

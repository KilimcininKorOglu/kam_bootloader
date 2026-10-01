/* KAM.INI parser. Single pass over lines, no allocation, bounded copies.
 * Returns the number of entries with a non-empty path. */

#include "kam/config.h"

static int kam_is_space(kam_u8 c) {
    return c == ' ' || c == '\t' || c == '\r';
}

static kam_usize kam_parse_uint(const kam_u8 *s, kam_usize n) {
    kam_usize v = 0;
    kam_usize i = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (kam_usize)(s[i] - '0');
        i++;
    }
    return v;
}

static int kam_word_eq(const kam_u8 *s, kam_usize n, const char *w) {
    kam_usize i = 0;
    while (w[i]) {
        if (i >= n)
            return 0;
        if (s[i] != (kam_u8)w[i])
            return 0;
        i++;
    }
    return i == n;
}

/* ASCII -> CHAR16 path copy with '/' fixups. Returns 0 when truncated. */
static int kam_copy_path(kam_char16 *dst, const kam_u8 *s, kam_usize n) {
    kam_usize i = 0, j = 0;
    if (n == 0 || n >= KAM_PATH_CHARS)
        return 1;
    if (s[0] != '\\' && s[0] != '/')
        dst[j++] = (kam_char16)'\\';
    for (i = 0; i < n && j + 1 < KAM_PATH_CHARS; i++) {
        kam_u8 c = s[i];
        dst[j++] = (kam_char16)(c == '/' ? '\\' : c);
    }
    dst[j] = 0;
    return i != n;
}

static void kam_drop_entry(kam_entry_t *out, kam_usize *count) {
    kam_usize i;
    if (*count == 0)
        return;
    if (out[*count - 1].path[0] != 0)
        return;
    (*count)--;
    for (i = 0; i < KAM_PATH_CHARS; i++) {
        out[*count].path[i] = 0;
        out[*count].initrd[i] = 0;
    }
    for (i = 0; i < KAM_LABEL_CHARS; i++)
        out[*count].label[i] = 0;
    for (i = 0; i < 128; i++)
        out[*count].cmdline[i] = 0;
}

kam_usize kam_config_parse(const kam_u8 *buf, kam_usize size,
                           kam_entry_t *out, kam_usize max,
                           kam_usize *timeout_out, kam_usize *def_out) {
    kam_usize count = 0;
    kam_usize timeout = 5;
    kam_usize def = 0;
    int section = -1; /* -1 none, 0 kernel, 1 chain, 2 iso, 3 linux */
    kam_usize off = 0;
    kam_usize i;

    for (i = 0; i < max; i++) {
        kam_usize j;
        out[i].kind = KAM_ENTRY_ELF;
        for (j = 0; j < KAM_PATH_CHARS; j++) {
            out[i].path[j] = 0;
            out[i].initrd[j] = 0;
        }
        for (j = 0; j < KAM_LABEL_CHARS; j++)
            out[i].label[j] = 0;
        for (j = 0; j < 128; j++)
            out[i].cmdline[j] = 0;
    }

    while (off < size) {
        kam_usize end = off;
        kam_usize a, b, k;
        while (end < size && buf[end] != '\n')
            end++;
        /* Trim [off, end). */
        a = off;
        b = end;
        while (a < b && kam_is_space(buf[a]))
            a++;
        while (b > a && kam_is_space(buf[b - 1]))
            b--;
        if (b - a > 0 && buf[a] != '#' && buf[a] != ';') {
            if (buf[a] == '[') {
                kam_drop_entry(out, &count);
                section = -1;
                if (b - a >= 2 && buf[b - 1] == ']') {
                    kam_usize nl = b - a - 2;
                    const kam_u8 *nm = buf + a + 1;
                    if (count < max) {
                        if (kam_word_eq(nm, nl, "kernel")) {
                            out[count].kind = KAM_ENTRY_ELF;
                            section = 0;
                            count++;
                        } else if (kam_word_eq(nm, nl, "chain")) {
                            out[count].kind = KAM_ENTRY_EFI;
                            section = 1;
                            count++;
                        } else if (kam_word_eq(nm, nl, "iso")) {
                            out[count].kind = KAM_ENTRY_ISO;
                            section = 2;
                            count++;
                        } else if (kam_word_eq(nm, nl, "linux")) {
                            out[count].kind = KAM_ENTRY_LINUX;
                            section = 3;
                            count++;
                        }
                    }
                }
            } else {
                /* key + value split on first blank. */
                k = a;
                while (k < b && !kam_is_space(buf[k]))
                    k++;
                {
                    kam_usize kl = k - a;
                    kam_usize va = k;
                    kam_usize vn;
                    while (va < b && kam_is_space(buf[va]))
                        va++;
                    vn = b - va;
                    if (section < 0) {
                        if (kam_word_eq(buf + a, kl, "timeout"))
                            timeout = kam_parse_uint(buf + va, vn);
                        else if (kam_word_eq(buf + a, kl, "default") &&
                                 kam_parse_uint(buf + va, vn) > 0)
                            def = kam_parse_uint(buf + va, vn) - 1;
                    } else if (count > 0) {
                        kam_entry_t *e = &out[count - 1];
                        if (kam_word_eq(buf + a, kl, "label") && vn > 0) {
                            kam_usize n = vn;
                            if (n >= KAM_LABEL_CHARS)
                                n = KAM_LABEL_CHARS - 1;
                            for (k = 0; k < n; k++)
                                e->label[k] = (char)buf[va + k];
                            e->label[n] = 0;
                        } else if (kam_word_eq(buf + a, kl, "path") &&
                                   vn > 0) {
                            kam_copy_path(e->path, buf + va, vn);
                        } else if (kam_word_eq(buf + a, kl, "initrd") &&
                                   vn > 0) {
                            kam_copy_path(e->initrd, buf + va, vn);
                        } else if (kam_word_eq(buf + a, kl, "cmdline") &&
                                   vn > 0) {
                            kam_usize n = vn;
                            if (n >= 128)
                                n = 128 - 1;
                            for (k = 0; k < n; k++)
                                e->cmdline[k] = (char)buf[va + k];
                            e->cmdline[n] = 0;
                        }
                    }
                }
            }
        }
        off = end + 1;
    }
    kam_drop_entry(out, &count);
    if (timeout > 60)
        timeout = 60;
    *timeout_out = timeout;
    *def_out = def;
    return count;
}

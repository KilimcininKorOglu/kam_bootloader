/* KAM boot log. Linear buffer (stops when full), raw serial mirror,
 * file flush through FileProtocol. */

#include "kam/bootlog.h"
#include "kam/efi.h"
#include "kam/console.h"

static kam_u8 kam_log_buf[KAM_LOG_SIZE];
static kam_usize kam_log_len;
static kam_system_table_t *kam_log_st;
static int kam_log_con;

void kam_log_init(struct kam_system_table *st) {
    kam_log_st = st;
    kam_log_con = 1;
}

void kam_log_no_conout(void) {
    kam_log_con = 0;
}

static const kam_char16 KAM_LOG_PATH[] = {
    '\\', 'K', 'A', 'M', '\\', 'B', 'O', 'O', 'T', '.', 'L', 'O', 'G', 0};

#define KAM_EFI_FILE_MODE_RW_CREATE \
    (1u | 2u | ((kam_u64)1u << 63))

void kam_log(const char *s) {
    const char *p = s;
    while (*p) {
        if (kam_log_len + 1 < KAM_LOG_SIZE)
            kam_log_buf[kam_log_len++] = (kam_u8)*p;
        p++;
    }
    if (kam_log_con && kam_log_st && kam_log_st->con_out &&
        kam_log_st->con_out->output_string)
        kam_puts(kam_log_st, s);
}

void kam_log_u64(kam_u64 v) {
    char buf[20];
    int i = 0;
    if (v == 0) {
        kam_log("0");
        return;
    }
    while (v > 0 && i < 20) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) {
        char tmp[2] = {buf[--i], 0};
        kam_log(tmp);
    }
}

void kam_log_hex(kam_u64 v) {
    kam_usize i;
    kam_log("0x");
    for (i = 0; i < 16; i++) {
        kam_u8 n = (kam_u8)((v >> 60) & 0xF);
        char tmp[2] = {(char)(n < 10 ? '0' + n : 'a' + (n - 10)), 0};
        kam_log(tmp);
        v <<= 4;
    }
}

int kam_log_flush(struct kam_boot_services *bs, struct kam_file_proto *root) {
    kam_file_proto_t *f = 0;
    kam_usize left;
    kam_status_t s;
    (void)bs;

    if (!root || !root->open)
        return 0;
    /* Delete any previous log so a shorter boot leaves no tail. */
    if (!KAM_EFI_ERROR(root->open(root, &f, KAM_LOG_PATH,
                                  KAM_EFI_FILE_MODE_READ, 0)) &&
        f && f->del)
        f->del(f);
    f = 0;
    s = root->open(root, &f, KAM_LOG_PATH, KAM_EFI_FILE_MODE_RW_CREATE, 0);
    if (KAM_EFI_ERROR(s) || !f || !f->write)
        return 0;
    left = kam_log_len;
    {
        kam_u8 *p = kam_log_buf;
        while (left > 0) {
            kam_usize chunk = left;
            s = f->write(f, &chunk, p);
            if (KAM_EFI_ERROR(s) || chunk == 0)
                return 0;
            p += chunk;
            left -= chunk;
        }
    }
    if (f->close)
        f->close(f);
    return 1;
}

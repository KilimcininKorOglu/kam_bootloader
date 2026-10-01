#ifndef KAM_CONSOLE_H
#define KAM_CONSOLE_H

#include "efi.h"

/* Print ASCII to the UEFI console. Converts narrow string -> CHAR16. */
static inline void kam_puts(kam_system_table_t *st, const char *s) {
    kam_char16 buf[128];
    kam_usize i = 0;
    if (!st || !st->con_out || !st->con_out->output_string)
        return;
    while (*s) {
        buf[i++] = (kam_char16)(*s++);
        if (i == 126 || *s == '\n') {
            if (*s == '\n') {
                buf[i++] = (kam_char16)'\r';
                buf[i++] = (kam_char16)'\n';
                s++;
            }
            buf[i] = 0;
            st->con_out->output_string(st->con_out, buf);
            i = 0;
            if (!*s)
                break;
        }
    }
    if (i) {
        buf[i] = 0;
        st->con_out->output_string(st->con_out, buf);
    }
}

#endif

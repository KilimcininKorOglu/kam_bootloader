#ifndef KAM_BIOS_CONSOLE_H
#define KAM_BIOS_CONSOLE_H

/* Dual console for the 64-bit BIOS payload: VGA text buffer + COM1 serial.
 * No libc, no BSS reliance (cursor is assigned at init, never zero-assumed). */

#include "types.h"
#include "bios_addrs.h"

#define KAM_VGA_BASE ((volatile kam_u16 *)0xB8000u)
#define KAM_VGA_COLS 80u
#define KAM_VGA_ROWS 25u

static kam_u8 kam_vga_row;
static kam_u8 kam_vga_col;

static inline void kam_outb(kam_u16 port, kam_u8 v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline kam_u8 kam_inb(kam_u16 port) {
    kam_u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void kam_serial_init(void) {
    kam_outb(0x3FB, 0x80);
    kam_outb(0x3F8, 0x01); /* divisor 1 = 115200 */
    kam_outb(0x3F9, 0x00);
    kam_outb(0x3FB, 0x03); /* 8N1 */
}

static inline void kam_serial_putc(char c) {
    while ((kam_inb(0x3FD) & 0x20) == 0) {
    }
    kam_outb(0x3F8, (kam_u8)c);
}

static inline void kam_vga_clear(void) {
    kam_u32 i;
    for (i = 0; i < KAM_VGA_COLS * KAM_VGA_ROWS; i++)
        KAM_VGA_BASE[i] = (kam_u16)0x0720;
    kam_vga_row = 0;
    kam_vga_col = 0;
}

static inline void kam_vga_putc(char c) {
    if (c == '\n') {
        kam_vga_col = 0;
        if (kam_vga_row + 1 < KAM_VGA_ROWS)
            kam_vga_row++;
        return;
    }
    KAM_VGA_BASE[(kam_u32)kam_vga_row * KAM_VGA_COLS + kam_vga_col] =
        (kam_u16)(0x0700u | (kam_u8)c);
    if (++kam_vga_col >= KAM_VGA_COLS) {
        kam_vga_col = 0;
        if (kam_vga_row + 1 < KAM_VGA_ROWS)
            kam_vga_row++;
    }
}

static inline void kam_cons_init(void) {
    kam_serial_init();
    kam_vga_clear();
}

static inline void kam_putc(char c) {
    if (c == '\n')
        kam_serial_putc('\r');
    kam_serial_putc(c);
    kam_vga_putc(c);
}

static inline void kam_puts(const char *s) {
    while (*s)
        kam_putc(*s++);
}

static inline void kam_put_u64(kam_u64 v) {
    char buf[20];
    int i = 0;
    if (v == 0) {
        kam_putc('0');
        return;
    }
    while (v > 0 && i < 20) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0)
        kam_putc(buf[--i]);
}

static inline void kam_put_hex(kam_u64 v) {
    kam_usize i;
    kam_putc('0');
    kam_putc('x');
    for (i = 0; i < 16; i++) {
        kam_u8 n = (kam_u8)((v >> 60) & 0xF);
        kam_putc((char)(n < 10 ? '0' + n : 'a' + (n - 10)));
        v <<= 4;
    }
}

#endif

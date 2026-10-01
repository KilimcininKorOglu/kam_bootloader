#ifndef KAM_RAW_SERIAL_H
#define KAM_RAW_SERIAL_H

/* Bare-metal serial for contexts without firmware: post-ExitBootServices
 * UEFI app and the test kernel. COM1 on x86_64, PL011 on QEMU virt
 * AArch64 (platform-specific; virt-only for now). */

#include "types.h"
#include "cpu.h"

#if defined(__x86_64__)

static inline void kam_raw_serial_init(void) {
    kam_cpu_outb(0x3FB, 0x80);
    kam_cpu_outb(0x3F8, 0x01);
    kam_cpu_outb(0x3F9, 0x00);
    kam_cpu_outb(0x3FB, 0x03);
}

static inline void kam_raw_putc(char c) {
    while ((kam_cpu_inb(0x3FD) & 0x20) == 0) {
    }
    kam_cpu_outb(0x3F8, (kam_u8)c);
}

#elif defined(__aarch64__)

#define KAM_PL011_BASE ((volatile kam_u32 *)0x09000000u)

static inline void kam_raw_serial_init(void) {
}

static inline void kam_raw_putc(char c) {
    while ((KAM_PL011_BASE[0x18 / 4] & (1u << 5)) != 0) {
    }
    KAM_PL011_BASE[0] = (kam_u32)c;
}

#else

static inline void kam_raw_serial_init(void) {
}

static inline void kam_raw_putc(char c) {
    (void)c;
}

#endif

static inline void kam_raw_puts(const char *s) {
    while (*s) {
        if (*s == '\n')
            kam_raw_putc('\r');
        kam_raw_putc(*s++);
    }
}

static inline void kam_raw_put_u64(kam_u64 v) {
    char buf[20];
    int i = 0;
    if (v == 0) {
        kam_raw_putc('0');
        return;
    }
    while (v > 0 && i < 20) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0)
        kam_raw_putc(buf[--i]);
}

static inline void kam_raw_halt(void) {
    for (;;)
        kam_cpu_halt();
}

#endif

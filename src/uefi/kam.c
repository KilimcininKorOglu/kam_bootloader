/* KAM UEFI application. x86_64 + AArch64 from a single source.
 * Freestanding C, no libc.
 *
 * Flow: banner -> GetMemoryMap (converted to kam_memmap) ->
 * ExitBootServices (key-retry loop) -> post-exit serial proof -> halt.
 * After ExitBootServices no firmware service (including ConOut) is used. */

#include "kam/efi.h"
#include "kam/console.h"
#include "kam/memmap.h"

#if defined(__x86_64__)
#define KAM_ARCH_NAME "x86_64 UEFI"
#elif defined(__aarch64__)
#define KAM_ARCH_NAME "AArch64 UEFI"
#else
#define KAM_ARCH_NAME "unknown UEFI"
#endif

#define KAM_MAP_BUF_SIZE 8192u

static kam_u8 kam_map_buf[KAM_MAP_BUF_SIZE];
static kam_memmap_t kam_map;

static void kam_put_hex(kam_system_table_t *st, kam_u64 v) {
    kam_usize i;
    kam_puts(st, "0x");
    for (i = 0; i < 16; i++) {
        kam_u8 n = (kam_u8)((v >> 60) & 0xF);
        char c = (char)(n < 10 ? '0' + n : 'a' + (n - 10));
        char tmp[2] = {c, 0};
        kam_puts(st, tmp);
        v <<= 4;
    }
}

static void kam_put_u64(kam_system_table_t *st, kam_u64 v) {
    char buf[20];
    int i = 0;
    if (v == 0) {
        kam_puts(st, "0");
        return;
    }
    while (v > 0 && i < 20) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) {
        char tmp[2] = {buf[--i], 0};
        kam_puts(st, tmp);
    }
}

/* Fill kam_map from GetMemoryMap. Returns the map key for ExitBootServices. */
static kam_status_t kam_fill_map(kam_system_table_t *st, kam_usize *key_out) {
    kam_boot_services_t *bs = st->boot;
    kam_usize size = KAM_MAP_BUF_SIZE;
    kam_usize key = 0;
    kam_usize desc_size = 0;
    kam_u32 desc_ver = 0;
    kam_usize n, i;
    kam_status_t s;

    if (!bs || !bs->get_map)
        return KAM_EFI_UNSUPPORTED;
    s = bs->get_map(&size, (kam_mem_desc_t *)kam_map_buf, &key,
                    &desc_size, &desc_ver);
    if (KAM_EFI_ERROR(s))
        return s;
    if (desc_size < sizeof(kam_mem_desc_t))
        return KAM_EFI_DEVICE_ERROR;

    n = size / desc_size;
    if (n > KAM_MEMMAP_MAX)
        n = KAM_MEMMAP_MAX;
    kam_map.count = (kam_u32)n;
    kam_map._pad = 0;
    for (i = 0; i < n; i++) {
        const kam_mem_desc_t *d =
            (const kam_mem_desc_t *)(kam_map_buf + i * desc_size);
        kam_map.entries[i].base = d->phys;
        kam_map.entries[i].len = d->pages * 4096u;
        kam_map.entries[i].type =
            (d->type == KAM_EFI_CONVENTIONAL) ? 1u : d->type;
        kam_map.entries[i].flags = (kam_u32)(d->attr & 0xFFFFFFFFu);
    }
    *key_out = key;
    return KAM_EFI_SUCCESS;
}

/* Direct serial output that survives ExitBootServices (no firmware used). */
#if defined(__x86_64__)
static kam_u8 kam_inb(kam_u16 port) {
    kam_u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static void kam_outb(kam_u16 port, kam_u8 v) {
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static void kam_raw_serial_init(void) {
    kam_outb(0x3FB, 0x80);
    kam_outb(0x3F8, 0x01);
    kam_outb(0x3F9, 0x00);
    kam_outb(0x3FB, 0x03);
}

static void kam_raw_putc_x86(char c) {
    while ((kam_inb(0x3FD) & 0x20) == 0) {
    }
    kam_outb(0x3F8, (kam_u8)c);
}
#elif defined(__aarch64__)
/* PL011 UART on QEMU virt (platform-specific; virt-only for now). */
#define KAM_PL011_BASE ((volatile kam_u32 *)0x09000000u)

static void kam_raw_putc_aa64(char c) {
    while ((KAM_PL011_BASE[0x18 / 4] & (1u << 5)) != 0) {
    }
    KAM_PL011_BASE[0] = (kam_u32)c;
}
#endif

static void kam_raw_puts(const char *s) {
    while (*s) {
#if defined(__x86_64__)
        if (*s == '\n')
            kam_raw_putc_x86('\r');
        kam_raw_putc_x86(*s++);
#elif defined(__aarch64__)
        if (*s == '\n')
            kam_raw_putc_aa64('\r');
        kam_raw_putc_aa64(*s++);
#else
        s++;
#endif
    }
}

static void kam_halt(void) {
#if defined(__x86_64__)
    for (;;)
        __asm__ volatile("hlt");
#elif defined(__aarch64__)
    for (;;)
        __asm__ volatile("wfi");
#else
    for (;;) {
    }
#endif
}

/* UEFI entry point: the linker script makes this symbol the entry. */
kam_status_t efi_main(kam_handle_t image, kam_system_table_t *st) {
    kam_usize key = 0;
    kam_status_t s;
    kam_u32 i;
    kam_u64 free_bytes = 0;
    int tries;

    if (!st || !st->con_out || !st->boot)
        return KAM_EFI_UNSUPPORTED;

    /* No screen clear: ClearScreen is optional on some firmware. */
    kam_puts(st, "KAM " KAM_ARCH_NAME "\n");

    s = kam_fill_map(st, &key);
    if (KAM_EFI_ERROR(s)) {
        kam_puts(st, "KAM: GetMemoryMap failed\n");
        return s;
    }
    for (i = 0; i < kam_map.count; i++) {
        kam_puts(st, "MEM ");
        kam_put_hex(st, kam_map.entries[i].base);
        kam_puts(st, " len ");
        kam_put_hex(st, kam_map.entries[i].len);
        kam_puts(st, " type ");
        kam_put_u64(st, kam_map.entries[i].type);
        kam_puts(st, "\n");
        if (kam_map.entries[i].type == 1)
            free_bytes += kam_map.entries[i].len;
    }
    kam_puts(st, "Free RAM: ");
    kam_put_u64(st, free_bytes);
    kam_puts(st, " bytes\n");

    /* ExitBootServices with map-key retry (the map may change under us). */
    for (tries = 0; tries < 3; tries++) {
        s = st->boot->exit_bs(image, key);
        if (!KAM_EFI_ERROR(s))
            break;
        s = kam_fill_map(st, &key);
        if (KAM_EFI_ERROR(s))
            return s;
    }
    if (KAM_EFI_ERROR(s)) {
        kam_puts(st, "KAM: ExitBootServices failed\n");
        return s;
    }

    /* Firmware console is dead from here on. Direct serial only. */
#if defined(__x86_64__)
    kam_raw_serial_init();
#endif
    kam_raw_puts("KAM: boot services exited, halting.\n");
    kam_halt();
    return KAM_EFI_SUCCESS;
}

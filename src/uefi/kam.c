/* KAM UEFI application. x86_64 + AArch64 from a single source.
 * Freestanding C, no libc.
 *
 * Flow: banner -> scan ESP (KERNEL.ELF first, then *.EFI) -> menu ->
 * ELF direct-boot (map, alloc, ExitBootServices, jump) or EFI chainload
 * (LoadImage/StartImage, stays in boot services). */

#include "kam/efi.h"
#include "kam/console.h"
#include "kam/memmap.h"
#include "kam/elf.h"
#include "kam/raw_serial.h"
#include "kam/scan.h"

#if defined(__x86_64__)
#define KAM_ARCH_NAME "x86_64 UEFI"
#elif defined(__aarch64__)
#define KAM_ARCH_NAME "AArch64 UEFI"
#else
#define KAM_ARCH_NAME "unknown UEFI"
#endif

#define KAM_MAP_BUF_SIZE 8192u
#define KAM_KERNEL_MAX 131072u /* 128KB static read buffer */

static kam_u8 kam_map_buf[KAM_MAP_BUF_SIZE];
static kam_memmap_t kam_map;
static kam_u8 kam_info_buf[128];
static kam_entry_t kam_entries[KAM_SCAN_MAX];

static kam_usize kam_strlen16(const kam_char16 *s) {
    kam_usize n = 0;
    while (s[n])
        n++;
    return n;
}

static void kam_put_path(kam_system_table_t *st, const kam_char16 *p) {
    char tmp[2] = {0, 0};
    while (*p) {
        tmp[0] = (char)(*p < 128 ? *p : '?');
        kam_puts(st, tmp);
        p++;
    }
}

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

/* Menu over scanned entries. Returns the chosen index (timeout: 0). */
static kam_usize kam_menu(kam_system_table_t *st, kam_usize count) {
    kam_text_in_t *cin = st->con_in;
    kam_key_t key;
    kam_status_t s;
    kam_usize i;
    int t;

    for (i = 0; i < count; i++) {
        kam_put_u64(st, (kam_u64)(i + 1));
        kam_puts(st, ") ");
        kam_put_path(st, kam_entries[i].path);
        kam_puts(st, kam_entries[i].kind == KAM_ENTRY_ELF ? " [elf]\n"
                                                          : " [efi]\n");
    }
    if (!cin || !cin->read_key || !st->boot->stall) {
        kam_puts(st, "No input services, booting default.\n");
        return 0;
    }
    kam_puts(st, "Booting 1 in 5s, press 1-9.\n");
    for (t = 0; t < 50; t++) {
        s = cin->read_key(cin, &key);
        if (s == KAM_EFI_SUCCESS && key.unicode >= (kam_char16)'1' &&
            key.unicode <= (kam_char16)'9') {
            kam_usize sel = (kam_usize)(key.unicode - (kam_char16)'1');
            if (sel < count)
                return sel;
        }
        st->boot->stall(100000);
    }
    return 0;
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

/* Read a whole file (CHAR16 path) into an AllocatePool buffer. */
static kam_status_t kam_read_file(kam_boot_services_t *bs,
                                  kam_file_proto_t *root,
                                  const kam_char16 *path, kam_u8 **img_out,
                                  kam_usize *size_out) {
    kam_file_proto_t *f = 0;
    kam_file_info_t *info = (kam_file_info_t *)kam_info_buf;
    kam_usize info_size = sizeof(kam_info_buf);
    kam_usize size;
    kam_u8 *buf = 0;
    kam_status_t s;

    s = root->open(root, &f, path, KAM_EFI_FILE_MODE_READ, 0);
    if (KAM_EFI_ERROR(s) || !f)
        return KAM_EFI_NOT_FOUND;
    s = f->getinfo(f, &KAM_GUID_FILE_INFO, &info_size, info);
    if (KAM_EFI_ERROR(s) || info->filesize == 0 ||
        info->filesize > KAM_KERNEL_MAX)
        return KAM_EFI_DEVICE_ERROR;
    size = (kam_usize)info->filesize;
    s = bs->alloc_pool(KAM_EFI_LOADER_DATA, size, (void **)&buf);
    if (KAM_EFI_ERROR(s) || !buf)
        return KAM_EFI_DEVICE_ERROR;
    s = f->read(f, &size, buf);
    if (KAM_EFI_ERROR(s) || size != (kam_usize)info->filesize)
        return KAM_EFI_DEVICE_ERROR;
    *img_out = buf;
    *size_out = size;
    return KAM_EFI_SUCCESS;
}

typedef void (*kam_kernel_fn)(const kam_memmap_t *map);

/* ELF direct-boot: map, load, ExitBootServices, jump. Never returns. */
static kam_status_t kam_boot_elf(kam_handle_t image, kam_system_table_t *st,
                                 kam_file_proto_t *root,
                                 const kam_char16 *path) {
    kam_boot_services_t *bs = st->boot;
    kam_usize key = 0;
    kam_status_t s;
    kam_u32 i;
    kam_u64 free_bytes = 0;
    kam_u8 *kimg = 0;
    kam_usize ksize = 0;
    kam_seg_t segs[KAM_ELF_MAXSEG];
    kam_usize nseg = 0;
    kam_u64 entry;
    int tries;

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

    s = kam_read_file(bs, root, path, &kimg, &ksize);
    if (KAM_EFI_ERROR(s)) {
        kam_puts(st, "KAM: kernel file missing\n");
        return s;
    }
    kam_puts(st, "KAM: kernel file bytes: ");
    kam_put_u64(st, ksize);
    kam_puts(st, "\n");
    entry = kam_elf_prepare(kimg, ksize, segs, &nseg);
    if (entry == 0) {
        kam_puts(st, "KAM: bad kernel ELF\n");
        return KAM_EFI_UNSUPPORTED;
    }

    /* Allocate the whole span once: segments may share pages. */
    {
        kam_u64 lo = ~(kam_u64)0, hi = 0;
        kam_u64 pages, addr;
        for (i = 0; i < nseg; i++) {
            kam_u64 b = segs[i].paddr & ~(kam_u64)4095u;
            kam_u64 e =
                (segs[i].paddr + segs[i].memsz + 4095u) & ~(kam_u64)4095u;
            if (b < lo)
                lo = b;
            if (e > hi)
                hi = e;
        }
        pages = (hi - lo) / 4096u;
        addr = lo;
        s = bs->alloc_pages(KAM_ALLOC_ADDRESS, KAM_EFI_LOADER_DATA, pages,
                            &addr);
        if (KAM_EFI_ERROR(s) || addr != lo) {
            kam_puts(st, "KAM: segment alloc failed\n");
            return KAM_EFI_DEVICE_ERROR;
        }
    }
    kam_elf_commit(kimg, segs, nseg);

    /* ExitBootServices with map-key retry (the map may change under us). */
    for (tries = 0; tries < 3; tries++) {
        s = bs->exit_bs(image, key);
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
    kam_raw_serial_init();
    kam_raw_puts("KAM: jumping to kernel.\n");
    ((kam_kernel_fn)entry)(&kam_map);
    kam_raw_puts("KAM: kernel returned, halting.\n");
    kam_raw_halt();
    return KAM_EFI_SUCCESS;
}

/* EFI chainload: our FilePath + FILEPATH node, LoadImage + StartImage. */
static kam_status_t kam_chainload(kam_system_table_t *st,
                                  kam_boot_services_t *bs,
                                  kam_handle_t image,
                                  const kam_char16 *path) {
    kam_loaded_image_t *li = 0;
    const kam_u8 *base;
    const kam_u8 *p;
    kam_usize prefix, n, node_len, total;
    kam_u8 *buf = 0;
    kam_handle_t child = 0;
    kam_status_t s;
    kam_usize i;

    s = bs->handle_proto(image, &KAM_GUID_LOADED_IMAGE, (void **)&li);
    if (KAM_EFI_ERROR(s) || !li)
        return KAM_EFI_NOT_FOUND;
    /* Our own FilePath is relative; resolve against the device path. */
    s = bs->handle_proto(li->dev_handle, &KAM_GUID_DEVPATH, (void **)&base);
    if (KAM_EFI_ERROR(s) || !base)
        return KAM_EFI_NOT_FOUND;
    p = base;
    for (;;) {
        kam_usize len = (kam_usize)p[2] | ((kam_usize)p[3] << 8);
        if (p[0] == 0x7F && p[1] == 0xFF)
            break;
        if (len < 4)
            return KAM_EFI_DEVICE_ERROR;
        p += len;
    }
    prefix = (kam_usize)(p - base);
    n = kam_strlen16(path);
    node_len = 4 + (n + 1) * 2;
    total = prefix + node_len + 4;
    s = bs->alloc_pool(KAM_EFI_LOADER_DATA, total, (void **)&buf);
    if (KAM_EFI_ERROR(s) || !buf)
        return KAM_EFI_DEVICE_ERROR;
    for (i = 0; i < prefix; i++)
        buf[i] = base[i];
    buf[prefix + 0] = 4;
    buf[prefix + 1] = 4;
    buf[prefix + 2] = (kam_u8)(node_len & 0xFF);
    buf[prefix + 3] = (kam_u8)((node_len >> 8) & 0xFF);
    for (i = 0; i <= n; i++) {
        buf[prefix + 4 + i * 2] = (kam_u8)(path[i] & 0xFF);
        buf[prefix + 4 + i * 2 + 1] = (kam_u8)((path[i] >> 8) & 0xFF);
    }
    buf[prefix + node_len + 0] = 0x7F;
    buf[prefix + node_len + 1] = 0xFF;
    buf[prefix + node_len + 2] = 4;
    buf[prefix + node_len + 3] = 0;
    s = bs->load_image(0, image, buf, 0, 0, &child);
    if (KAM_EFI_ERROR(s) || !child)
        return s;
    return bs->start_image(child, 0, 0);
}

/* UEFI entry point: the linker script makes this symbol the entry. */
kam_status_t efi_main(kam_handle_t image, kam_system_table_t *st) {
    kam_boot_services_t *bs;
    kam_file_proto_t *root = 0;
    kam_usize count;
    kam_usize sel;
    kam_status_t s;

    if (!st || !st->con_out || !st->boot)
        return KAM_EFI_UNSUPPORTED;
    bs = st->boot;

    /* No screen clear: ClearScreen is optional on some firmware. */
    kam_puts(st, "KAM " KAM_ARCH_NAME "\n");

    count = kam_scan(bs, image, kam_entries, KAM_SCAN_MAX);
    if (count == 0) {
        kam_puts(st, "KAM: nothing bootable found\n");
        return KAM_EFI_NOT_FOUND;
    }
    sel = kam_menu(st, count);
    if (KAM_EFI_ERROR(kam_fs_open_root(bs, image, &root)) || !root) {
        kam_puts(st, "KAM: no volume\n");
        return KAM_EFI_NOT_FOUND;
    }
    if (kam_entries[sel].kind == KAM_ENTRY_ELF)
        return kam_boot_elf(image, st, root, kam_entries[sel].path);
    kam_puts(st, "KAM: chainloading ");
    kam_put_path(st, kam_entries[sel].path);
    kam_puts(st, "\n");
    s = kam_chainload(st, bs, image, kam_entries[sel].path);
    kam_puts(st, "KAM: chainload returned\n");
    return s;
}

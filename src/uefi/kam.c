/* KAM UEFI application. x86_64 + AArch64 from a single source.
 * Freestanding C, no libc.
 *
 * Flow: banner -> INI config + multi-volume scan -> menu ->
 * ELF direct-boot, bzImage direct-boot, ISO probe, or EFI chainload. */

#include "kam/efi.h"
#include "kam/console.h"
#include "kam/memmap.h"
#include "kam/elf.h"
#include "kam/raw_serial.h"
#include "kam/scan.h"
#include "kam/iso.h"
#include "kam/config.h"
#include "kam/gop.h"
#include "kam/linux.h"
#include "kam/sha256.h"
#include "kam/bootlog.h"

#if defined(__x86_64__)
#define KAM_ARCH_NAME "x86_64 UEFI"
#elif defined(__aarch64__)
#define KAM_ARCH_NAME "AArch64 UEFI"
#else
#define KAM_ARCH_NAME "unknown UEFI"
#endif

#define KAM_MAP_REQ_MAX (1u << 20) /* 1MB sanity cap on map bytes */
#define KAM_KERNEL_MAX 131072u /* 128KB static read buffer */

typedef kam_status_t (*kam_free_pool_fn)(void *p);
static kam_memmap_t kam_map;
static kam_u8 kam_info_buf[128];
static kam_entry_t kam_scanout[KAM_SCAN_MAX];
static kam_config_t kam_cfg;

static const kam_char16 KAM_INI_PATH[] = {
    '\\', 'K', 'A', 'M', '\\', 'K', 'A', 'M', '.', 'I', 'N', 'I', 0};

static int kam_path_eq(const kam_char16 *a, const kam_char16 *b) {
    kam_usize i = 0;
    for (;;) {
        kam_u8 ca = (kam_u8)(a[i] > 127 ? 0 : a[i]);
        kam_u8 cb = (kam_u8)(b[i] > 127 ? 0 : b[i]);
        if (ca >= 'a' && ca <= 'z')
            ca -= 32u;
        if (cb >= 'a' && cb <= 'z')
            cb -= 32u;
        if (ca != cb)
            return 0;
        if (ca == 0)
            return 1;
        i++;
    }
}

static kam_usize kam_strlen16(const kam_char16 *s) {
    kam_usize n = 0;
    while (s[n])
        n++;
    return n;
}

static void kam_put_path(kam_system_table_t *st, const kam_char16 *p) {
    char tmp[2] = {0, 0};
    (void)st;
    while (*p) {
        tmp[0] = (char)(*p < 128 ? *p : '?');
        kam_log(tmp);
        p++;
    }
}

static void kam_put_hex(kam_system_table_t *st, kam_u64 v) {
    kam_usize i;
    (void)st;
    kam_log("0x");
    for (i = 0; i < 16; i++) {
        kam_u8 n = (kam_u8)((v >> 60) & 0xF);
        char c = (char)(n < 10 ? '0' + n : 'a' + (n - 10));
        char tmp[2] = {c, 0};
        kam_log(tmp);
        v <<= 4;
    }
}

static void kam_put_u64(kam_system_table_t *st, kam_u64 v) {
    char buf[20];
    int i = 0;
    (void)st;
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

/* Menu over entries. Timeout boots def_idx. Returns chosen index. */
static kam_usize kam_menu(kam_system_table_t *st, kam_usize count,
                          kam_usize timeout, kam_usize def) {
    kam_text_in_t *cin = st->con_in;
    kam_key_t key;
    kam_status_t s;
    kam_usize i;
    int t, polls;

    for (i = 0; i < count; i++) {
        kam_put_u64(st, (kam_u64)(i + 1));
        kam_log(") [hd");
        kam_put_u64(st, (kam_u64)kam_cfg.entries[i].vol);
        kam_log("] ");
        kam_log(kam_cfg.entries[i].label);
        kam_log(kam_cfg.entries[i].kind == KAM_ENTRY_ELF ? " [elf]\n"
                 : kam_cfg.entries[i].kind == KAM_ENTRY_EFI  ? " [efi]\n"
                 : kam_cfg.entries[i].kind == KAM_ENTRY_ISO  ? " [iso]\n"
                                                        : " [linux]\n");
    }
    if (def >= count)
        def = 0;
    if (timeout == 0) {
        kam_log("Booting default now.\n");
        return def;
    }
    if (timeout > 60)
        timeout = 60;
    if (!cin || !cin->read_key || !st->boot->stall) {
        kam_log("No input services, booting default.\n");
        return def;
    }
    kam_log("Booting ");
    kam_put_u64(st, (kam_u64)(def + 1));
    kam_log(" in ");
    kam_put_u64(st, (kam_u64)timeout);
    if (count > 9)
        kam_log("s, press 1-9,a-f.\n");
    else
        kam_log("s, press 1-9.\n");
    polls = (int)(timeout * 10);
    for (t = 0; t < polls; t++) {
        s = cin->read_key(cin, &key);
        if (s == KAM_EFI_SUCCESS) {
            kam_usize sel = count;
            if (key.unicode >= (kam_char16)'1' &&
                key.unicode <= (kam_char16)'9')
                sel = (kam_usize)(key.unicode - (kam_char16)'1');
            else if (key.unicode >= (kam_char16)'a' &&
                     key.unicode <= (kam_char16)'f')
                sel = (kam_usize)(9 + key.unicode - (kam_char16)'a');
            if (sel < count)
                return sel;
        }
        st->boot->stall(100000);
    }
    return def;
}

/* Fill kam_map from GetMemoryMap. Starts with a generous pool buffer and
 * grows to the firmware-reported size on BUFFER_TOO_SMALL, so large maps
 * (many descriptors) work instead of failing on a fixed static buffer.
 * Returns the map key for ExitBootServices. */
static kam_status_t kam_fill_map(kam_system_table_t *st, kam_usize *key_out) {
    kam_boot_services_t *bs = st->boot;
    kam_mem_desc_t *buf = 0;
    kam_usize buf_size = 65536u;
    kam_usize size = 0;
    kam_usize key = 0;
    kam_usize desc_size = 0;
    kam_u32 desc_ver = 0;
    kam_usize n, i;
    kam_status_t s;
    int tries;

    if (!bs || !bs->get_map || !bs->alloc_pool)
        return KAM_EFI_UNSUPPORTED;
    s = bs->alloc_pool(KAM_EFI_LOADER_DATA, buf_size, (void **)&buf);
    if (KAM_EFI_ERROR(s) || !buf)
        return KAM_EFI_DEVICE_ERROR;
    for (tries = 0; tries < 3; tries++) {
        size = buf_size;
        s = bs->get_map(&size, buf, &key, &desc_size, &desc_ver);
        if (!KAM_EFI_ERROR(s))
            break;
        if (s != KAM_EFI_BUFFER_TOO_SMALL || size <= buf_size ||
            size > KAM_MAP_REQ_MAX)
            break;
        if (bs->free_pool)
            ((kam_free_pool_fn)bs->free_pool)(buf);
        buf = 0;
        buf_size = size;
        s = bs->alloc_pool(KAM_EFI_LOADER_DATA, buf_size, (void **)&buf);
        if (KAM_EFI_ERROR(s) || !buf)
            return KAM_EFI_DEVICE_ERROR;
    }
    if (KAM_EFI_ERROR(s)) {
        if (buf && bs->free_pool)
            ((kam_free_pool_fn)bs->free_pool)(buf);
        return s;
    }
    if (desc_size < sizeof(kam_mem_desc_t)) {
        if (bs->free_pool)
            ((kam_free_pool_fn)bs->free_pool)(buf);
        return KAM_EFI_DEVICE_ERROR;
    }

    n = size / desc_size;
    if (n > KAM_MEMMAP_MAX)
        n = KAM_MEMMAP_MAX;
    kam_map.count = (kam_u32)n;
    kam_map._pad = 0;
    for (i = 0; i < n; i++) {
        const kam_mem_desc_t *d =
            (const kam_mem_desc_t *)((const kam_u8 *)buf + i * desc_size);
        kam_map.entries[i].base = d->phys;
        kam_map.entries[i].len = d->pages * 4096u;
        kam_map.entries[i].type =
            (d->type == KAM_EFI_CONVENTIONAL) ? 1u : d->type;
        kam_map.entries[i].flags = (kam_u32)(d->attr & 0xFFFFFFFFu);
    }
    if (bs->free_pool)
        ((kam_free_pool_fn)bs->free_pool)(buf);
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

/* Boot password gate. Hidden entry with '*' echo, SHA-256(salt + pw)
 * against the KAM.INI hash, constant-time compare, 3 tries then halt
 * (returning would fall through to the next firmware boot option). */
static void kam_password_gate(kam_system_table_t *st) {
    kam_text_in_t *cin;
    int tries;
    if (!kam_cfg.has_pw)
        return;
    cin = st->con_in;
    if (!cin || !cin->read_key || !st->boot->stall) {
        kam_log("KAM: no input for password, halting.\n");
        kam_raw_halt();
    }
    for (tries = 0; tries < 3; tries++) {
        char pw[64];
        kam_usize len = 0;
        kam_u8 salted[128];
        kam_u8 digest[32];
        kam_usize slen = 0;
        kam_usize i;
        kam_log("Password: ");
        for (;;) {
            kam_key_t key;
            kam_status_t s = cin->read_key(cin, &key);
            if (s != KAM_EFI_SUCCESS) {
                st->boot->stall(50000);
                continue;
            }
            if (key.unicode == (kam_char16)'\r')
                break;
            if (key.unicode == (kam_char16)'\b') {
                if (len > 0)
                    len--;
                continue;
            }
            if (key.unicode >= 32 && key.unicode <= 126 && len + 1 < 64) {
                pw[len++] = (char)key.unicode;
                kam_log("*");
            }
        }
        pw[len] = 0;
        kam_log("\n");
        while (kam_cfg.pw_salt[slen] && slen < 63)
            slen++;
        for (i = 0; i < slen; i++)
            salted[i] = (kam_u8)kam_cfg.pw_salt[i];
        for (i = 0; i < len; i++)
            salted[slen + i] = (kam_u8)pw[i];
        kam_sha256_once(salted, slen + len, digest);
        for (i = 0; i < 64; i++)
            pw[i] = 0;
        if (kam_ct_eq(digest, kam_cfg.pw_hash, 32)) {
            for (i = 0; i < 32; i++)
                digest[i] = 0;
            return;
        }
        for (i = 0; i < 32; i++)
            digest[i] = 0;
        kam_log("KAM: wrong password\n");
    }
    kam_log("KAM: access denied\n");
    kam_raw_halt();
}

/* ISO probe: read the file, verify volume + catalog, report. */
static void kam_thunk_puts(const char *s) {
    kam_log(s);
}

static void kam_thunk_putc(char c) {
    char tmp[2] = {c, 0};
    kam_log(tmp);
}

static void kam_thunk_putu(kam_u64 v) {
    kam_log_u64(v);
}

/* ISO probe: read the file, verify volume + catalog, report. */
static kam_status_t kam_boot_iso(kam_boot_services_t *bs,
                                 kam_file_proto_t *root,
                                 const kam_char16 *path) {
    kam_u8 *img = 0;
    kam_usize size = 0;
    kam_status_t s;

    s = kam_read_file(bs, root, path, &img, &size);
    if (KAM_EFI_ERROR(s)) {
        kam_log("KAM: iso file missing\n");
        return s;
    }
    if (kam_iso_boot_report(img, size, kam_thunk_puts, kam_thunk_putc,
                            kam_thunk_putu)) {
        kam_log("KAM: bad ISO image\n");
        return KAM_EFI_UNSUPPORTED;
    }
    return KAM_EFI_SUCCESS;
}

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
        kam_log("KAM: GetMemoryMap failed\n");
        return s;
    }
    for (i = 0; i < kam_map.count; i++) {
        kam_log("MEM ");
        kam_put_hex(st, kam_map.entries[i].base);
        kam_log(" len ");
        kam_put_hex(st, kam_map.entries[i].len);
        kam_log(" type ");
        kam_put_u64(st, kam_map.entries[i].type);
        kam_log("\n");
        if (kam_map.entries[i].type == 1)
            free_bytes += kam_map.entries[i].len;
    }
    kam_log("Free RAM: ");
    kam_put_u64(st, free_bytes);
    kam_log(" bytes\n");

    s = kam_read_file(bs, root, path, &kimg, &ksize);
    if (KAM_EFI_ERROR(s)) {
        kam_log("KAM: kernel file missing\n");
        return s;
    }
    kam_log("KAM: kernel file bytes: ");
    kam_put_u64(st, ksize);
    kam_log("\n");
    entry = kam_elf_prepare(kimg, ksize, segs, &nseg);
    if (entry == 0) {
        kam_log("KAM: bad kernel ELF\n");
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
            kam_log("KAM: segment alloc failed\n");
            return KAM_EFI_DEVICE_ERROR;
        }
    }
    kam_elf_commit(kimg, segs, nseg);

    /* Paint the header last: on GOP consoles ConOut text shares the
     * framebuffer, so earlier text would dirty the art. After this only
     * direct serial is used, leaving the art pristine for the kernel. */
    {
        kam_u32 gw = 0, gh = 0;
        if (kam_gop_draw(bs, &gw, &gh)) {
            kam_log("GOP ");
            kam_put_u64(st, (kam_u64)gw);
            kam_log("x");
            kam_put_u64(st, (kam_u64)gh);
            kam_log("\n");
        }
    }

    /* ExitBootServices with map-key retry (the map may change under us). */
    kam_log("KAM: exiting boot services.\n");
    kam_log_no_conout();
    kam_log_flush(bs, root);
    for (tries = 0; tries < 3; tries++) {
        s = bs->exit_bs(image, key);
        if (!KAM_EFI_ERROR(s))
            break;
        s = kam_fill_map(st, &key);
        if (KAM_EFI_ERROR(s))
            return s;
    }
    if (KAM_EFI_ERROR(s)) {
        kam_log("KAM: ExitBootServices failed\n");
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

/* EFI chainload: volume device path + FILEPATH node, LoadImage/StartImage. */
static kam_status_t kam_chainload(kam_boot_services_t *bs,
                                  kam_handle_t image, kam_handle_t dev,
                                  const kam_char16 *path) {
    const kam_u8 *base;
    const kam_u8 *p;
    kam_usize prefix, n, node_len, total;
    kam_u8 *buf = 0;
    kam_handle_t child = 0;
    kam_status_t s;
    kam_usize i;

    /* Resolve against the entry's own volume device path. */
    s = bs->handle_proto(dev, &KAM_GUID_DEVPATH, (void **)&base);
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

/* Windows layout probe: existence checks for the well-known markers.
 * bootmgfw.efi itself is chainloaded through the normal .EFI path;
 * WinPE remount + driver injection happen inside Windows, out of scope. */
static int kam_try_open(kam_file_proto_t *root, const kam_char16 *path) {
    kam_file_proto_t *f = 0;
    kam_status_t s;
    if (!root || !root->open)
        return 0;
    s = root->open(root, &f, path, KAM_EFI_FILE_MODE_READ, 0);
    return !KAM_EFI_ERROR(s) && f != 0;
}

static const kam_char16 KAM_P_MGFW[] = {
    '\\', 'E', 'F', 'I', '\\', 'M', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't',
    '\\', 'B', 'o', 'o', 't', '\\', 'b', 'o', 'o', 't', 'm', 'g', 'f', 'w',
    '.', 'e', 'f', 'i', 0};
static const kam_char16 KAM_P_BCD[] = {
    '\\', 'E', 'F', 'I', '\\', 'M', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't',
    '\\', 'B', 'o', 'o', 't', '\\', 'B', 'C', 'D', 0};
static const kam_char16 KAM_P_WIM[] = {
    '\\', 's', 'o', 'u', 'r', 'c', 'e', 's', '\\', 'i', 'n', 's', 't', 'a',
    'l', 'l', '.', 'w', 'i', 'm', 0};
static const kam_char16 KAM_P_ESD[] = {
    '\\', 's', 'o', 'u', 'r', 'c', 'e', 's', '\\', 'i', 'n', 's', 't', 'a',
    'l', 'l', '.', 'e', 's', 'd', 0};

static void kam_win_probe(kam_system_table_t *st, kam_file_proto_t *root) {
    int mgfw, bcd, wim, esd;
    mgfw = kam_try_open(root, KAM_P_MGFW);
    bcd = kam_try_open(root, KAM_P_BCD);
    wim = kam_try_open(root, KAM_P_WIM);
    esd = kam_try_open(root, KAM_P_ESD);
    if (!mgfw && !bcd && !wim && !esd)
        return;
    kam_log("WIN markers mgfw=");
    kam_put_u64(st, (kam_u64)mgfw);
    kam_log(" bcd=");
    kam_put_u64(st, (kam_u64)bcd);
    kam_log(" wim=");
    kam_put_u64(st, (kam_u64)wim);
    kam_log(" esd=");
    kam_put_u64(st, (kam_u64)esd);
    kam_log("\n");
}

/* UEFI entry point: the linker script makes this symbol the entry. */
kam_status_t efi_main(kam_handle_t image, kam_system_table_t *st) {
    kam_boot_services_t *bs;
    kam_file_proto_t *root = 0;
    kam_loaded_image_t *li = 0;
    kam_handle_t our_dev = 0;
    kam_usize count = 0;
    kam_usize timeout = 5;
    kam_usize def = 0;
    kam_usize sel;
    kam_usize nscan, i;
    kam_status_t s;
    kam_handle_t sel_dev = 0;

    if (!st || !st->con_out || !st->boot)
        return KAM_EFI_UNSUPPORTED;
    bs = st->boot;
    kam_log_init(st);

    /* No screen clear: ClearScreen is optional on some firmware. */
    kam_log("KAM " KAM_ARCH_NAME "\n");

    if (KAM_EFI_ERROR(kam_fs_open_root(bs, image, &root)) || !root) {
        kam_log("KAM: no volume\n");
        return KAM_EFI_NOT_FOUND;
    }
    if (!KAM_EFI_ERROR(
            bs->handle_proto(image, &KAM_GUID_LOADED_IMAGE, (void **)&li)) &&
        li)
        our_dev = li->dev_handle;
    kam_win_probe(st, root);

    /* Static config first: KAM/KAM.INI. Missing file = dynamic only. */
    kam_cfg.count = 0;
    kam_cfg.timeout = 5;
    kam_cfg.def = 0;
    kam_cfg.has_pw = 0;
    {
        kam_u8 *cfg = 0;
        kam_usize cfg_size = 0;
        if (!KAM_EFI_ERROR(kam_read_file(bs, root, KAM_INI_PATH, &cfg,
                                         &cfg_size)) &&
            cfg_size <= 8192) {
            kam_config_parse(cfg, cfg_size, &kam_cfg);
            kam_log("KAM: config entries: ");
            kam_put_u64(st, (kam_u64)kam_cfg.count);
            kam_log("\n");
        }
    }
    count = kam_cfg.count;
    timeout = kam_cfg.timeout;
    def = kam_cfg.def;

    /* Append scanned files not already listed (same volume + path). */
    nscan = kam_scan_all(bs, image, kam_scanout, KAM_SCAN_MAX);
    for (i = 0; i < nscan && count < KAM_SCAN_MAX; i++) {
        kam_usize j;
        int dup = 0;
        for (j = 0; j < count; j++) {
            if (kam_scanout[i].vol == kam_cfg.entries[j].vol &&
                kam_path_eq(kam_scanout[i].path, kam_cfg.entries[j].path)) {
                dup = 1;
                break;
            }
        }
        if (!dup) {
            /* Field copy: no memcpy in freestanding. */
            kam_usize f;
            kam_cfg.entries[count].kind = kam_scanout[i].kind;
            kam_cfg.entries[count].dev = kam_scanout[i].dev;
            kam_cfg.entries[count].vol = kam_scanout[i].vol;
            for (f = 0; f < KAM_PATH_CHARS; f++) {
                kam_cfg.entries[count].path[f] = kam_scanout[i].path[f];
                kam_cfg.entries[count].initrd[f] = kam_scanout[i].initrd[f];
            }
            for (f = 0; f < KAM_LABEL_CHARS; f++)
                kam_cfg.entries[count].label[f] = kam_scanout[i].label[f];
            for (f = 0; f < 128; f++)
                kam_cfg.entries[count].cmdline[f] = kam_scanout[i].cmdline[f];
            count++;
        }
    }
    if (count == 0) {
        kam_log("KAM: nothing bootable found\n");
        return KAM_EFI_NOT_FOUND;
    }
    sel = kam_menu(st, count, timeout, def);
    kam_password_gate(st);
    sel_dev = kam_cfg.entries[sel].dev ? kam_cfg.entries[sel].dev : our_dev;
    if (sel_dev && sel_dev != our_dev) {
        kam_file_proto_t *eroot = 0;
        if (KAM_EFI_ERROR(kam_fs_open_volume(bs, sel_dev, &eroot)) ||
            !eroot) {
            kam_log("KAM: volume unavailable\n");
            return KAM_EFI_NOT_FOUND;
        }
        root = eroot;
    }
    if (kam_cfg.entries[sel].kind == KAM_ENTRY_ELF)
        return kam_boot_elf(image, st, root, kam_cfg.entries[sel].path);
    if (kam_cfg.entries[sel].kind == KAM_ENTRY_LINUX) {
        kam_u8 *img = 0, *ird = 0;
        kam_usize isize = 0, rsize = 0;
        kam_usize key = 0;
        kam_u64 img_addr = 0, ird_addr = 0, params_addr = 0;
        const char *cmd;
        int bz;
        kam_log("KAM: direct linux boot\n");
        bz = kam_bz_claim(bs, &img_addr, &ird_addr, &params_addr);
        if (bz <= 0) {
            kam_log("KAM: bzImage claim failed stage ");
            kam_put_u64(st, (kam_u64)(kam_u32)(-bz));
            kam_log(" status ");
            kam_put_hex(st, (kam_u64)kam_bz_last_status);
            kam_log(" want ");
            kam_put_hex(st, kam_bz_last_want);
            kam_log("\n");
            return KAM_EFI_DEVICE_ERROR;
        }
        if (KAM_EFI_ERROR(kam_fill_map(st, &key))) {
            kam_log("KAM: GetMemoryMap failed\n");
            return KAM_EFI_DEVICE_ERROR;
        }
        if (KAM_EFI_ERROR(
                kam_read_file(bs, root, kam_cfg.entries[sel].path, &img,
                              &isize))) {
            kam_log("KAM: kernel file missing\n");
            return KAM_EFI_NOT_FOUND;
        }
        if (kam_cfg.entries[sel].initrd[0] &&
            KAM_EFI_ERROR(kam_read_file(bs, root, kam_cfg.entries[sel].initrd,
                                         &ird, &rsize))) {
            kam_log("KAM: initrd file missing\n");
            return KAM_EFI_NOT_FOUND;
        }
        cmd = kam_cfg.entries[sel].cmdline[0] ? kam_cfg.entries[sel].cmdline
                                          : "kam-test";
        kam_log("KAM: jumping to bzImage.\n");
        kam_log_flush(bs, root);
        bz = kam_bz_boot(bs, img, isize, ird, rsize, cmd, &kam_map,
                         img_addr, ird_addr, params_addr);
        if (bz <= 0) {
            kam_log("KAM: bzImage refused stage ");
            kam_put_u64(st, (kam_u64)(kam_u32)(-bz));
            kam_log("\n");
            return KAM_EFI_UNSUPPORTED;
        }
        return KAM_EFI_SUCCESS;
    }
    if (kam_cfg.entries[sel].kind == KAM_ENTRY_ISO) {
        kam_log("KAM: probing ");
        kam_put_path(st, kam_cfg.entries[sel].path);
        kam_log("\n");
        s = kam_boot_iso(bs, root, kam_cfg.entries[sel].path);
        kam_log("KAM: iso probe returned\n");
        kam_log_flush(bs, root);
        return s;
    }
    kam_log("KAM: chainloading ");
    kam_put_path(st, kam_cfg.entries[sel].path);
    kam_log("\n");
    s = kam_chainload(bs, image, sel_dev ? sel_dev : our_dev,
                      kam_cfg.entries[sel].path);
    kam_log("KAM: chainload returned\n");
    kam_log_flush(bs, root);
    return s;
}

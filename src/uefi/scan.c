/* KAM ESP scanner. Walks directories via FileProtocol reads, collects
 * KERNEL.ELF first and every other *.EFI after. Freestanding, no alloc:
 * fixed buffers, bounded depth, bounded entries. */

#include "kam/scan.h"

#define KAM_SCAN_BUF 512u
#define KAM_INFO_NAME_OFF 80u
#define KAM_ATTR_DIR 0x10u

static kam_u8 kam_scan_buf[KAM_SCAN_BUF];

static kam_usize kam_strlen16(const kam_char16 *s) {
    kam_usize n = 0;
    while (s[n])
        n++;
    return n;
}

static kam_u8 kam_up(kam_u8 c) {
    return (kam_u8)(c >= 'a' && c <= 'z' ? c - 32u : c);
}

/* ASCII tail match, case-insensitive (name is CHAR16). */
static int kam_tail_eq(const kam_char16 *name, const char *tail) {
    kam_usize nl = kam_strlen16(name);
    kam_usize tl = 0;
    kam_usize i;
    while (tail[tl])
        tl++;
    if (tl > nl)
        return 0;
    for (i = 0; i < tl; i++) {
        if (kam_up((kam_u8)name[nl - tl + i]) != kam_up((kam_u8)tail[i]))
            return 0;
    }
    return 1;
}

static int kam_name_is(const kam_char16 *name, const char *want) {
    kam_usize i = 0;
    while (want[i]) {
        if (kam_up((kam_u8)name[i]) != kam_up((kam_u8)want[i]))
            return 0;
        i++;
    }
    return name[i] == 0;
}

kam_status_t kam_fs_open_root(kam_boot_services_t *bs, kam_handle_t image,
                              kam_file_proto_t **root_out) {
    kam_loaded_image_t *li = 0;
    kam_fs_proto_t *fs = 0;
    kam_status_t s;

    s = bs->handle_proto(image, &KAM_GUID_LOADED_IMAGE, (void **)&li);
    if (KAM_EFI_ERROR(s) || !li)
        return KAM_EFI_NOT_FOUND;
    s = bs->handle_proto(li->dev_handle, &KAM_GUID_SIMPLE_FS, (void **)&fs);
    if (KAM_EFI_ERROR(s) || !fs || !fs->open_volume)
        return KAM_EFI_NOT_FOUND;
    return fs->open_volume(fs, root_out);
}

/* Append prefix + '\' + name into entry path. 0 = ok. */
static int kam_join(kam_char16 *dst, const kam_char16 *prefix,
                    const kam_char16 *name) {
    kam_usize i = 0, j = 0;
    while (prefix[i] && i + 1 < KAM_PATH_CHARS) {
        dst[i] = prefix[i];
        i++;
    }
    if (i + 1 < KAM_PATH_CHARS)
        dst[i++] = (kam_char16)'\\';
    while (name[j] && i + 1 < KAM_PATH_CHARS)
        dst[i++] = name[j++];
    dst[i] = 0;
    return name[j] != 0;
}

static kam_status_t kam_walk(kam_file_proto_t *dir, const kam_char16 *prefix,
                             int depth, int pass, kam_entry_t *out,
                             kam_usize max, kam_usize *count) {
    kam_usize size;
    kam_status_t s;

    for (;;) {
        kam_usize off = 0;
        size = sizeof(kam_scan_buf);
        s = dir->read(dir, &size, kam_scan_buf);
        if (KAM_EFI_ERROR(s) || size == 0)
            return s;
        /* One Read may pack several EFI_FILE_INFO structs. */
        while (off + KAM_INFO_NAME_OFF + 4 <= size) {
            kam_file_info_t *info;
            const kam_char16 *name;
            kam_u64 attr;
            info = (kam_file_info_t *)(kam_scan_buf + off);
            if (info->size == 0 || info->size > size - off)
                break;
            if (info->size >= KAM_INFO_NAME_OFF + 4) {
                name = (const kam_char16 *)(kam_scan_buf + off +
                                            KAM_INFO_NAME_OFF);
                if (name[0] != 0 && !kam_name_is(name, ".") &&
                    !kam_name_is(name, "..")) {
                    attr = *(const kam_u64 *)(kam_scan_buf + off + 72);
                    if (attr & KAM_ATTR_DIR) {
                        kam_file_proto_t *sub = 0;
                        kam_char16 subpath[KAM_PATH_CHARS];
                        if (depth + 1 < KAM_SCAN_DEPTH &&
                            !kam_join(subpath, prefix, name)) {
                            s = dir->open(dir, &sub, name,
                                          KAM_EFI_FILE_MODE_READ, 0);
                            if (!KAM_EFI_ERROR(s) && sub)
                                kam_walk(sub, subpath, depth + 1, pass,
                                         out, max, count);
                        }
                    } else {
                        int is_kernel = kam_tail_eq(name, "KERNEL.ELF");
                        int is_efi = kam_tail_eq(name, ".EFI");
                        int is_iso = kam_tail_eq(name, ".ISO");
                        int is_self = kam_tail_eq(name, "BOOTX64.EFI") ||
                                      kam_tail_eq(name, "BOOTAA64.EFI");
                        int want = (pass == 0) ? is_kernel
                                 : (pass == 1) ? (!is_kernel && is_efi && !is_self)
                                               : (is_iso && !is_kernel && !is_efi);
                        if (*count < max && want &&
                            !kam_join(out[*count].path, prefix, name)) {
                            kam_usize s, e;
                            out[*count].kind = is_kernel ? KAM_ENTRY_ELF
                                             : is_efi   ? KAM_ENTRY_EFI
                                                        : KAM_ENTRY_ISO;
                            /* Label = basename, ASCII. */
                            s = 0;
                            e = 0;
                            while (out[*count].path[e]) {
                                if (out[*count].path[e] == (kam_char16)'\\' ||
                                    out[*count].path[e] == (kam_char16)'/')
                                    s = e + 1;
                                e++;
                            }
                            {
                                kam_usize li = 0;
                                while (s + li < e &&
                                       li + 1 < KAM_LABEL_CHARS) {
                                    kam_char16 c = out[*count].path[s + li];
                                    out[*count].label[li] =
                                        (char)(c < 128 ? c : '?');
                                    li++;
                                }
                                out[*count].label[li] = 0;
                            }
                            (*count)++;
                        }
                    }
                }
            }
            off += info->size;
        }
    }
}

kam_usize kam_scan(kam_boot_services_t *bs, kam_handle_t image,
                   kam_entry_t *out, kam_usize max) {
    kam_file_proto_t *root = 0;
    kam_usize count = 0;
    kam_char16 empty[1] = {0};
    int pass;

    if (bs->handle_proto == 0)
        return 0;
    if (KAM_EFI_ERROR(kam_fs_open_root(bs, image, &root)) || !root)
        return 0;
    for (pass = 0; pass < 3 && count < max; pass++) {
        if (pass > 0 && root->setpos)
            root->setpos(root, 0);
        kam_walk(root, empty, 0, pass, out, max, &count);
    }
    return count;
}

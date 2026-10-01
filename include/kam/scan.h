#ifndef KAM_SCAN_H
#define KAM_SCAN_H

/* ESP scan: collect bootable files into a small entry list.
 * Pass 1 pins KAM/KERNEL.ELF (or any KERNEL.ELF) as entry 0;
 * pass 2 adds every other *.EFI for chainloading. */

#include "efi.h"

#define KAM_SCAN_MAX 16u
#define KAM_SCAN_DEPTH 4
#define KAM_PATH_CHARS 96u
#define KAM_LABEL_CHARS 64u

typedef enum kam_entry_kind {
    KAM_ENTRY_ELF,
    KAM_ENTRY_EFI,
    KAM_ENTRY_ISO,
    KAM_ENTRY_LINUX
} kam_entry_kind_t;

typedef struct kam_entry {
    kam_entry_kind_t kind;
    kam_char16 path[KAM_PATH_CHARS];
    char label[KAM_LABEL_CHARS];
    kam_char16 initrd[KAM_PATH_CHARS];
    char cmdline[128];
    kam_handle_t dev; /* volume device handle, 0 = our own volume */
    kam_u32 vol;      /* volume tag for display (0 = ours first) */
} kam_entry_t;

/* Open the volume our image was loaded from. */
kam_status_t kam_fs_open_root(kam_boot_services_t *bs, kam_handle_t image,
                              kam_file_proto_t **root_out);

/* Open the volume behind a device handle. */
kam_status_t kam_fs_open_volume(kam_boot_services_t *bs, kam_handle_t dev,
                                kam_file_proto_t **root_out);

/* Fill out[] (cap max), return entry count. */
kam_usize kam_scan(kam_boot_services_t *bs, kam_handle_t image,
                   kam_entry_t *out, kam_usize max);

/* Scan every SimpleFileSystem volume; our own volume sorts first (vol 0).
 * Entries carry their volume handle for per-volume chainload. */
kam_usize kam_scan_all(kam_boot_services_t *bs, kam_handle_t image,
                       kam_entry_t *out, kam_usize max);

#endif

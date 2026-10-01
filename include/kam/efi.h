#ifndef KAM_EFI_H
#define KAM_EFI_H

/* KAM minimal UEFI definitions. */

#include "types.h"

typedef kam_u16 kam_char16; /* UEFI wide char */

typedef struct kam_guid {
    kam_u32 data1;
    kam_u16 data2;
    kam_u16 data3;
    kam_u8  data4[8];
} kam_guid_t;

typedef void *kam_handle_t;

/* EFI_STATUS: top bit = error. */
typedef kam_uintn kam_status_t;
#define KAM_EFI_SUCCESS            ((kam_status_t)0)
#define KAM_EFI_ERROR_BIT          ((kam_status_t)1 << (sizeof(kam_status_t) * 8 - 1))
#define KAM_EFI_BUFFER_TOO_SMALL   (KAM_EFI_ERROR_BIT | 5)
#define KAM_EFI_NOT_FOUND          (KAM_EFI_ERROR_BIT | 14)
#define KAM_EFI_UNSUPPORTED        (KAM_EFI_ERROR_BIT | 3)
#define KAM_EFI_DEVICE_ERROR       (KAM_EFI_ERROR_BIT | 7)

#define KAM_EFI_ERROR(s) (((kam_status_t)(s) & KAM_EFI_ERROR_BIT) != 0)

struct kam_simple_text_out;

/* SIMPLE_TEXT_OUTPUT_PROTOCOL: only what we need. */
typedef kam_status_t (*kam_text_out_fn)(
    struct kam_simple_text_out *self, const kam_char16 *str);
typedef kam_status_t (*kam_text_reset_fn)(
    struct kam_simple_text_out *self, kam_u8 extended);

typedef struct kam_simple_text_out {
    void             *reset_placeholder; /* Call Reset through the signature below */
    kam_text_out_fn   output_string;
    void             *test_string;
    void             *query_mode;
    void             *set_mode;
    void             *set_attribute;
    void             *clear_screen;
    void             *set_cursor;
    void             *enable_cursor;
    void             *mode;
} kam_simple_text_out_t;

/* Forward: full definition follows below (needed by kam_system_table_t). */
typedef struct kam_boot_services kam_boot_services_t;

/* SYSTEM_TABLE: minimal layout to reach ConsoleOut + BootServices.
 * Offsets follow the UEFI spec: Hdr is 24 bytes (not pointer-sized),
 * FirmwareRevision is 32-bit with 32-bit padding after it. */
typedef struct kam_system_table {
    kam_u8                  hdr[24]; /* Signature + Revision + Size + CRC + Reserved */
    kam_char16             *firmware_vendor;    /* offset 24 */
    kam_u32                 firmware_rev;        /* offset 32 */
    kam_u32                 _pad0;               /* offset 36 */
    kam_handle_t            console_in;          /* offset 40 */
    void                   *con_in;              /* offset 48 */
    kam_handle_t            console_out_handle;  /* offset 56 */
    kam_simple_text_out_t  *con_out;             /* offset 64 */
    kam_handle_t            stderr_handle;       /* offset 72 */
    void                   *con_err;             /* offset 80 */
    void                   *runtime;             /* offset 88 */
    kam_boot_services_t    *boot;               /* offset 96 */
} kam_system_table_t;

KAM_STATIC_ASSERT(sizeof(kam_system_table_t) == 104, system_table_size);

/* UEFI entry signature: efi_main(ImageHandle, SystemTable). */
typedef kam_status_t (*kam_efi_entry_t)(kam_handle_t, kam_system_table_t *);

/* EFI_MEMORY_DESCRIPTOR. Firmware may return a larger DescSize. */
typedef struct kam_mem_desc {
    kam_u32 type;
    kam_u32 _pad;
    kam_u64 phys;
    kam_u64 virt;
    kam_u64 pages;
    kam_u64 attr;
} kam_mem_desc_t;

KAM_STATIC_ASSERT(sizeof(kam_mem_desc_t) == 40, mem_desc_size);

/* Memory types we care about (EFI_MEMORY_TYPE). */
#define KAM_EFI_CONVENTIONAL 7u

typedef kam_status_t (*kam_get_map_fn)(
    kam_usize *size, kam_mem_desc_t *map, kam_usize *key,
    kam_usize *desc_size, kam_u32 *desc_ver);
typedef kam_status_t (*kam_exit_bs_fn)(kam_handle_t image, kam_usize key);

/* BOOT_SERVICES up to ExitBootServices. Field order follows the spec;
 * GetMemoryMap is at +56, ExitBootServices at +232. */
struct kam_boot_services {
    kam_u8 hdr[24];
    void *raise_tpl;        /* +24 */
    void *restore_tpl;      /* +32 */
    void *alloc_pages;      /* +40 */
    void *free_pages;       /* +48 */
    kam_get_map_fn get_map; /* +56 */
    void *alloc_pool;       /* +64 */
    void *free_pool;        /* +72 */
    void *create_event;     /* +80 */
    void *set_timer;        /* +88 */
    void *wait_event;       /* +96 */
    void *signal_event;     /* +104 */
    void *close_event;      /* +112 */
    void *check_event;      /* +120 */
    void *install_proto;    /* +128 */
    void *reinstall_proto;  /* +136 */
    void *uninstall_proto;  /* +144 */
    void *handle_proto;     /* +152 */
    void *reserved;         /* +160 */
    void *reg_notify;       /* +168 */
    void *locate_handle;    /* +176 */
    void *locate_devpath;   /* +184 */
    void *install_cfg;      /* +192 */
    void *load_image;       /* +200 */
    void *start_image;      /* +208 */
    void *exit;             /* +216 */
    void *unload_image;     /* +224 */
    kam_exit_bs_fn exit_bs; /* +232 */
};

KAM_STATIC_ASSERT(sizeof(kam_boot_services_t) == 240, boot_services_size);

#endif

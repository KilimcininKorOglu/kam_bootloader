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
struct kam_boot_services;
struct kam_system_table;

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

/* BOOT_SERVICES: only the head we need for now. */
typedef struct kam_boot_services {
    void *hdr[6]; /* Signature, revision, size... skipped, accessed by offset */
} kam_boot_services_raw_t;

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
    struct kam_boot_services *boot;              /* offset 96 */
} kam_system_table_t;

KAM_STATIC_ASSERT(sizeof(kam_system_table_t) == 104, system_table_size);

/* UEFI entry signature: efi_main(ImageHandle, SystemTable). */
typedef kam_status_t (*kam_efi_entry_t)(kam_handle_t, kam_system_table_t *);

#endif

/* KAM ISO9660 + El Torito probe. Bounds-checked throughout; any
 * out-of-range read fails the image instead of faulting. */

#include "kam/iso.h"

#define KAM_SECTOR 2048u

static kam_u16 kam_rd16(const kam_u8 *p) {
    return (kam_u16)((kam_u16)p[0] | ((kam_u16)p[1] << 8));
}

static kam_u32 kam_rd32(const kam_u8 *p) {
    return (kam_u32)p[0] | ((kam_u32)p[1] << 8) | ((kam_u32)p[2] << 16) |
           ((kam_u32)p[3] << 24);
}

static int kam_mem_eq(const kam_u8 *a, const kam_u8 *b, kam_usize n) {
    kam_usize i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i])
            return 0;
    }
    return 1;
}

/* Sector pointer with bounds check. 0 = ok. */
static int kam_sect(const kam_u8 *img, kam_usize size, kam_u32 lba,
                    const kam_u8 **out) {
    kam_u64 off = (kam_u64)lba * KAM_SECTOR;
    if (off + KAM_SECTOR > size)
        return 1;
    *out = img + off;
    return 0;
}

int kam_iso_boot_report(const kam_u8 *img, kam_usize size,
                        kam_iso_puts puts, kam_iso_putc putc,
                        kam_iso_putu putu) {
    const kam_u8 *pvd, *rec, *cat, *bootimg;
    kam_u32 root_lba, root_size, cat_lba, img_lba;
    kam_u64 off, end;
    int found = 0;

    /* Primary Volume Descriptor at LBA 16. */
    if (kam_sect(img, size, 16, &pvd))
        return 1;
    if (pvd[0] != 1 || !kam_mem_eq(pvd + 1, (const kam_u8 *)"CD001", 5))
        return 1;
    puts("ISO: PVD ok\n");

    /* Root directory record at PVD+156. */
    root_lba = kam_rd32(pvd + 156 + 2);
    root_size = kam_rd32(pvd + 156 + 10);
    if (kam_sect(img, size, root_lba, &rec))
        return 1;
    off = 0;
    end = root_size;
    {
        kam_u64 avail = size - (kam_u64)root_lba * KAM_SECTOR;
        if (end > avail)
            end = avail;
    }
    if (end > KAM_SECTOR)
        end = KAM_SECTOR;
    while (off + 30 <= end) {
        const kam_u8 *r = rec + off;
        kam_u8 len = r[0];
        kam_u8 flags, nlen;
        kam_u32 ext, sz;
        if (len == 0) {
            off = (off / KAM_SECTOR + 1) * KAM_SECTOR;
            continue;
        }
        if (len < 30 || off + len > end)
            break;
        /* Dot records are exactly 30 bytes with a 1-byte 0x00/0x01 name. */
        if (len == 30 && r[28] == 1 && r[29] <= 1) {
            off += len;
            continue;
        }
        /* Anything else this short cannot carry a name: corrupt. */
        if (len < 38)
            break;
        flags = r[25];
        nlen = r[28];
        ext = kam_rd32(r + 2);
        sz = kam_rd32(r + 10);
        if (!(flags & 2) && nlen >= 9 &&
            kam_mem_eq(r + 29, (const kam_u8 *)"HELLO.TXT", 9)) {
            const kam_u8 *data;
            kam_usize i;
            if (kam_sect(img, size, ext, &data))
                return 1;
            if (sz < 14 || !kam_mem_eq(data, (const kam_u8 *)"hello from iso", 14))
                return 1;
            puts("ISO file: ");
            for (i = 0; i < 14; i++)
                putc((char)data[i]);
            found = 1;
        }
        off += len;
    }
    if (!found)
        return 1;

    /* Boot Record at LBA 17, catalog pointer at +71. */
    if (kam_sect(img, size, 17, &rec))
        return 1;
    if (rec[0] != 0 || !kam_mem_eq(rec + 1, (const kam_u8 *)"CD001", 5))
        return 1;
    if (!kam_mem_eq(rec + 7, (const kam_u8 *)"EL TORITO SPECIFICATION", 23))
        return 1;
    cat_lba = kam_rd32(rec + 71);
    puts("ISO: El Torito catalog at ");
    putu(cat_lba);
    puts("\n");
    if (kam_sect(img, size, cat_lba, &cat))
        return 1;

    /* Validation entry: type 1, x86, checksum zero, key 0xAA55. */
    {
        kam_usize i;
        kam_u32 sum = 0;
        if (cat[0] != 1 || cat[1] != 0)
            return 1;
        for (i = 0; i < 16; i++)
            sum += kam_rd16(cat + i * 2);
        if ((sum & 0xFFFF) != 0)
            return 1;
        if (kam_rd16(cat + 30) != 0xAA55)
            return 1;
    }

    /* Initial entry at +32: bootable, no-emulation, LBA + sectors. */
    {
        const kam_u8 *e = cat + 32;
        kam_u16 sectors;
        if (e[0] != 0x88)
            return 1;
        puts("ISO: bootable, media ");
        putu(e[1]);
        sectors = kam_rd16(e + 6);
        img_lba = kam_rd32(e + 8);
        puts(" sectors ");
        putu(sectors);
        puts(" at ");
        putu(img_lba);
        puts("\n");
        if (kam_sect(img, size, img_lba, &bootimg))
            return 1;
        if (!kam_mem_eq(bootimg, (const kam_u8 *)"KAM-BOOT-IMG", 12))
            return 1;
    }

    puts("KAM-ISO-OK\n");
    return 0;
}

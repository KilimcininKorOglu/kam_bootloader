#!/usr/bin/env python3
"""KAM mkesp: build a partitioned ESP disk image in pure python (no mtools).
Usage: mkesp.py --out build/esp.img --x64 build/BOOTX64.EFI [--aa64 build/BOOTAA64.EFI]

Layout: MBR with one bootable FAT16 partition (type 0x0E) + FAT16 volume.
A partition table is required: OVMF/EDK2 only auto-boot
EFI/BOOT/BOOT{ARCH}.EFI from a partitioned hard disk, not a superfloppy.
"""
import argparse
import os
import struct
import sys

SECTOR = 512
PART_START = 2048  # 1MiB alignment, standard for ESPs

def build_fat16(files, total_sectors=131072, startup_nsh=None):  # 64MiB volume
    reserved = 4
    fats = 2
    sectors_per_fat = 256
    root_entries = 512
    sectors_per_cluster = 4  # 2KB clusters
    root_sectors = root_entries * 32 // SECTOR
    first_data = reserved + fats * sectors_per_fat + root_sectors

    vol = bytearray(total_sectors * SECTOR)

    # --- boot sector (BPB), LBA-relative to the partition start ---
    bpb = bytearray(SECTOR)
    bpb[0:3] = b'\xEB\x3C\x90'
    bpb[3:11] = b'KAMBOOT '
    struct.pack_into('<H', bpb, 11, SECTOR)
    bpb[13] = sectors_per_cluster
    struct.pack_into('<H', bpb, 14, reserved)
    bpb[16] = fats
    struct.pack_into('<H', bpb, 17, root_entries)
    struct.pack_into('<H', bpb, 19, total_sectors if total_sectors < 65536 else 0)
    bpb[21] = 0xF8
    struct.pack_into('<H', bpb, 22, sectors_per_fat)
    struct.pack_into('<H', bpb, 24, 63)
    struct.pack_into('<H', bpb, 26, 255)
    struct.pack_into('<I', bpb, 28, PART_START)  # hidden sectors
    struct.pack_into('<I', bpb, 32, total_sectors)
    bpb[36] = 0x80
    bpb[38] = 0x29
    struct.pack_into('<I', bpb, 39, 0x4B414D21)
    bpb[43:54] = b'KAM ESP    '
    bpb[54:62] = b'FAT16   '
    bpb[510:512] = b'\x55\xAA'
    vol[0:SECTOR] = bpb

    # --- FAT tables ---
    for f in range(fats):
        off = (reserved + f * sectors_per_fat) * SECTOR
        struct.pack_into('<HHH', vol, off, 0xFFF8, 0xFFFF, 0xFFFF)

    def fat_set(cluster, val):
        for f in range(fats):
            off = (reserved + f * sectors_per_fat) * SECTOR + cluster * 2
            struct.pack_into('<H', vol, off, val)

    def cluster_off(cluster):
        return (first_data + (cluster - 2) * sectors_per_cluster) * SECTOR

    # Simple allocator for files + directories.
    next_cluster = 2
    root = bytearray(root_sectors * SECTOR)

    def alloc_chain(nbytes):
        nonlocal next_cluster
        ncl = max(1, (nbytes + sectors_per_cluster * SECTOR - 1) // (sectors_per_cluster * SECTOR))
        start = next_cluster
        for i in range(ncl):
            c = next_cluster
            nxt = c + 1 if i < ncl - 1 else 0xFFFF
            fat_set(c, nxt)
            next_cluster += 1
        return start, ncl

    def write_chain(start, data):
        c = start
        off = 0
        while True:
            base = cluster_off(c)
            chunk = data[off:off + sectors_per_cluster * SECTOR]
            vol[base:base + len(chunk)] = chunk
            off += len(chunk)
            # Read next from the first FAT.
            foff = reserved * SECTOR + c * 2
            nxt = struct.unpack_from('<H', vol, foff)[0]
            if nxt >= 0xFFF8:
                break
            c = nxt

    def dos_name(name):
        name = name.upper()
        if '.' in name:
            base, ext = name.rsplit('.', 1)
        else:
            base, ext = name, ''
        return (base[:8].ljust(8) + ext[:3].ljust(3)).encode('ascii')

    def root_add(dosname, attr, cluster, size):
        for i in range(root_entries):
            o = i * 32
            if root[o] in (0x00, 0xE5):
                root[o:o + 11] = dosname
                root[o + 11] = attr
                struct.pack_into('<H', root, o + 20, 0)        # cluster high (FAT16: 0)
                struct.pack_into('<H', root, o + 26, cluster)  # cluster low
                struct.pack_into('<I', root, o + 28, size)     # file size
                return
        raise RuntimeError('root directory is full')

    # Directories: EFI, EFI/BOOT.
    def make_dir():
        start, _ = alloc_chain(sectors_per_cluster * SECTOR)
        d = bytearray(sectors_per_cluster * SECTOR)
        write_chain(start, d)
        return start

    efi_cl = make_dir()
    boot_cl = make_dir()
    root_add(dos_name('EFI'), 0x10, efi_cl, 0)
    # BOOT entry inside EFI.
    efi_data = bytearray(sectors_per_cluster * SECTOR)
    efi_data[0:11] = dos_name('BOOT')
    efi_data[11] = 0x10
    struct.pack_into('<H', efi_data, 26, boot_cl)
    write_chain(efi_cl, bytes(efi_data))

    boot_entries = []
    for fname, data in files:
        start, _ = alloc_chain(len(data))
        write_chain(start, data)
        boot_entries.append((dos_name(os.path.basename(fname)), start, len(data)))

    boot_data = bytearray(sectors_per_cluster * SECTOR)
    for i, (dn, cl, sz) in enumerate(boot_entries):
        o = i * 32
        boot_data[o:o + 11] = dn
        boot_data[o + 11] = 0x20
        struct.pack_into('<H', boot_data, o + 26, cl)
        struct.pack_into('<I', boot_data, o + 28, sz)
    write_chain(boot_cl, bytes(boot_data))

    # Optional shell fallback: the UEFI shell auto-runs STARTUP.NSH.
    # Needed for QEMU hard disks (non-removable, no auto Boot#### entry).
    if startup_nsh:
        data = startup_nsh.encode('ascii')
        start, _ = alloc_chain(len(data))
        write_chain(start, data)
        root_add(dos_name('STARTUP.NSH'), 0x20, start, len(data))

    # Root dir lives in the 32 sectors right before the data region.
    root_off = (first_data - root_sectors) * SECTOR
    vol[root_off:root_off + root_sectors * SECTOR] = root

    # --- MBR with one bootable FAT16-LBA partition ---
    disk_sectors = PART_START + total_sectors
    img = bytearray(disk_sectors * SECTOR)
    mbr = bytearray(SECTOR)
    struct.pack_into('<I', mbr, 440, 0x4B414D21)  # disk signature
    # Partition entry 0 at offset 446: bootable, type 0x0E, LBA range.
    p = 446
    mbr[p + 0] = 0x80          # boot flag
    mbr[p + 1:p + 4] = b'\xFF\xFF\xFF'  # start CHS (LBA-only)
    mbr[p + 4] = 0x0E          # FAT16 LBA
    mbr[p + 5:p + 8] = b'\xFF\xFF\xFF'  # end CHS (LBA-only)
    struct.pack_into('<I', mbr, p + 8, PART_START)
    struct.pack_into('<I', mbr, p + 12, total_sectors)
    mbr[510:512] = b'\x55\xAA'
    img[0:SECTOR] = mbr
    img[PART_START * SECTOR:] = vol
    return bytes(img)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--x64', default=None)
    ap.add_argument('--aa64', default=None)
    ap.add_argument('--startup-nsh', action='store_true',
                    help='add STARTUP.NSH fallback launching the default loader')
    a = ap.parse_args()
    files = []
    if a.x64:
        with open(a.x64, 'rb') as f:
            files.append(('BOOTX64.EFI', f.read()))
    if a.aa64:
        with open(a.aa64, 'rb') as f:
            files.append(('BOOTAA64.EFI', f.read()))
    if not files:
        print('mkesp: no EFI files to add', file=sys.stderr)
        return 1
    first = files[0][0]
    nsh = f'\\EFI\\BOOT\\{first}\r\n' if a.startup_nsh else None
    img = build_fat16(files, startup_nsh=nsh)
    os.makedirs(os.path.dirname(a.out) or '.', exist_ok=True)
    with open(a.out, 'wb') as f:
        f.write(img)
    print(f'mkesp: {a.out} ({len(img)//1024//1024}MB, {len(files)} files)')
    return 0

if __name__ == '__main__':
    sys.exit(main())

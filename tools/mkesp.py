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

def build_fat16(files, total_sectors=131072, startup_nsh=None, extras=None,
                superfloppy=False):  # 64MiB volume
    reserved = 4
    fats = 2
    root_entries = 512
    root_sectors = root_entries * 32 // SECTOR
    # Cluster size + FAT size: stay FAT16 (4085..65525 clusters).
    sectors_per_cluster, sectors_per_fat = 4, 256
    clusters = (total_sectors - reserved - fats * sectors_per_fat -
                root_sectors) // sectors_per_cluster
    if not 4085 <= clusters <= 65525:
        for sectors_per_cluster in (2, 1):
            sectors_per_fat = max(
                1, -(-((total_sectors // sectors_per_cluster + 2) * 2)
                     // SECTOR))
            clusters = (total_sectors - reserved - fats * sectors_per_fat -
                        root_sectors) // sectors_per_cluster
            if 4085 <= clusters <= 65525:
                break
        else:
            raise RuntimeError('volume too small for FAT16')
    first_data = (reserved + fats * sectors_per_fat + root_sectors)

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

    def needs_lfn(name):
        up = name.upper()
        if '.' in up:
            base, ext = up.rsplit('.', 1)
        else:
            base, ext = up, ''
        return len(base) > 8 or len(ext) > 3

    def lfn_checksum(dos11):
        s = 0
        for b in dos11:
            s = ((s >> 1) + ((s & 1) << 7) + b) & 0xFF
        return s

    def short_record(dos11, attr, cluster, size):
        rec = bytearray(32)
        rec[0:11] = dos11
        rec[11] = attr
        struct.pack_into('<H', rec, 20, 0)
        struct.pack_into('<H', rec, 26, cluster)
        struct.pack_into('<I', rec, 28, size)
        return bytes(rec)

    def entry_records(name, attr, cluster, size):
        """8.3 record, preceded by LFN records when the name needs them."""
        dos11 = dos_name(name)
        recs = []
        if needs_lfn(name):
            units = [ord(c) for c in name]
            n = (len(units) + 12) // 13
            padded = units + [0x0000] + [0xFFFF] * (n * 13 - len(units) - 1)
            for e in range(n):
                seq = n - e
                if e == 0:
                    seq |= 0x40
                chunk13 = padded[e * 13:(e + 1) * 13]
                rec = bytearray(32)
                rec[0] = seq
                rec[11] = 0x0F
                rec[13] = lfn_checksum(dos11)
                rec[1:11] = struct.pack('<5H', *chunk13[0:5])
                rec[14:26] = struct.pack('<6H', *chunk13[5:11])
                rec[28:32] = struct.pack('<2H', *chunk13[11:13])
                recs.append(bytes(rec))
        recs.append(short_record(dos11, attr, cluster, size))
        return recs

    def put_records(buf, max_slots, name, attr, cluster, size):
        """Write an 8.3 record plus LFN records when needed, contiguously."""
        recs = entry_records(name, attr, cluster, size)
        n = len(recs)
        for i in range(max_slots - n + 1):
            if all(buf[(i + k) * 32] in (0x00, 0xE5) for k in range(n)):
                for k, rec in enumerate(recs):
                    o = (i + k) * 32
                    buf[o:o + 32] = rec
                return
        raise RuntimeError('directory is full: ' + name)

    def root_add(name, attr, cluster, size):
        put_records(root, root_entries, name, attr, cluster, size)

    # Directories: EFI, EFI/BOOT.
    def make_dir():
        start, _ = alloc_chain(sectors_per_cluster * SECTOR)
        d = bytearray(sectors_per_cluster * SECTOR)
        write_chain(start, d)
        return start

    efi_cl = make_dir()
    boot_cl = make_dir()
    root_add('EFI', 0x10, efi_cl, 0)
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

    # Generic extras: 'NAME.EXT' goes to root, 'A/B/NAME.EXT' nested.
    # The EFI dir already exists (BOOT setup above); reuse it instead of
    # shadowing it with a duplicate entry.
    dir_bufs = {'EFI': [efi_cl, efi_data]}

    def find_in_buf(buf, ent):
        for i in range(len(buf) // 32):
            o = i * 32
            if buf[o] == 0x00:
                break
            if buf[o] == 0xE5:
                continue
            if bytes(buf[o:o + 11]) == ent and (buf[o + 11] & 0x10):
                return struct.unpack_from('<H', buf, o + 26)[0]
        return None

    def ensure_dir(prefix):
        if prefix in dir_bufs:
            return dir_bufs[prefix]
        parent, sep, leaf = prefix.rpartition('/')
        if sep:
            _, pbuf = ensure_dir(parent)
            on_root = False
        else:
            pbuf = root
            on_root = True
        ent = dos_name(leaf)
        cl = find_in_buf(pbuf, ent)
        if cl is None:
            cl = make_dir()
            buf = bytearray(sectors_per_cluster * SECTOR)
            if on_root:
                root_add(leaf, 0x10, cl, 0)
            else:
                put_records(pbuf, len(pbuf) // 32, leaf, 0x10, cl, 0)
        else:
            buf = None
            for c, b in dir_bufs.values():
                if c == cl:
                    buf = b
                    break
            if buf is None:
                raise RuntimeError('untracked existing dir: ' + prefix)
        dir_bufs[prefix] = [cl, buf]
        return dir_bufs[prefix]

    for fatpath, data in (extras or []):
        parts = fatpath.split('/')
        start, _ = alloc_chain(len(data))
        write_chain(start, data)
        if len(parts) == 1:
            root_add(parts[0], 0x20, start, len(data))
        else:
            _, buf = ensure_dir('/'.join(parts[:-1]))
            put_records(buf, len(buf) // 32, parts[-1], 0x20, start,
                        len(data))
    for cl, buf in dir_bufs.values():
        write_chain(cl, bytes(buf))

    # Optional shell fallback: the UEFI shell auto-runs STARTUP.NSH.
    # Needed for QEMU hard disks (non-removable, no auto Boot#### entry).
    if startup_nsh:
        data = startup_nsh.encode('ascii')
        start, _ = alloc_chain(len(data))
        write_chain(start, data)
        root_add('STARTUP.NSH', 0x20, start, len(data))

    # Root dir lives in the 32 sectors right before the data region.
    root_off = (first_data - root_sectors) * SECTOR
    vol[root_off:root_off + root_sectors * SECTOR] = root

    # --- MBR with one bootable FAT16-LBA partition (skipped for
    # El Torito EFI images: those must be plain FAT volumes) ---
    if superfloppy:
        return bytes(vol)
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
    ap.add_argument('--extra', action='append', default=[],
                    help='extra file as SRC:FATPATH (e.g. k.elf:KAM/KERNEL.ELF)')
    ap.add_argument('--sectors', type=int, default=131072,
                    help='volume size in sectors (default 131072 = 64MB)')
    ap.add_argument('--superfloppy', action='store_true',
                    help='omit the MBR, volume starts at offset 0 '
                         '(El Torito EFI boot images)')
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
    extras = []
    for item in a.extra:
        src, _, dst = item.partition(':')
        with open(src, 'rb') as f:
            extras.append((dst, f.read()))
    img = build_fat16(files, total_sectors=a.sectors, startup_nsh=nsh,
                      extras=extras, superfloppy=a.superfloppy)
    expect = a.sectors if a.superfloppy else (PART_START + a.sectors)
    assert len(img) == expect * SECTOR, len(img)
    os.makedirs(os.path.dirname(a.out) or '.', exist_ok=True)
    with open(a.out, 'wb') as f:
        f.write(img)
    print(f'mkesp: {a.out} ({len(img)//1024//1024}MB, {len(files)} files)')
    return 0

if __name__ == '__main__':
    sys.exit(main())

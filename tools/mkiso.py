#!/usr/bin/env python3
"""mkiso.py: build a minimal read-only ISO9660 test image for KAM.
Layout (2048-byte sectors):
  LBA 16: Primary Volume Descriptor (root dir at LBA 20)
  LBA 17: Boot Record (El Torito catalog at LBA 19)
  LBA 18: Terminator
  LBA 19: El Torito catalog (validation + one bootable no-emulation entry)
  LBA 20: root directory ('.', '..', 'HELLO.TXT;1' at LBA 22)
  LBA 21: boot image (1 sector, 'KAM-BOOT-IMG' marker)
  LBA 22: HELLO.TXT contents ('hello from iso')
Usage: mkiso.py --out build/test.iso
"""
import argparse
import struct
import sys

SECTOR = 2048


def fix(data, n):
    """Pad/truncate to exactly n bytes (slice assignment resizes on mismatch)."""
    if len(data) < n:
        return data + b"\x00" * (n - len(data))
    return data[:n]


def both16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)


def both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


def dir_record(extent, size, flags, name):
    r = bytearray()
    r += b"\x00"          # length (patched later)
    r += b"\x00"          # ext attr len
    r += both32(extent)
    r += both32(size)
    r += b"\x00" * 7      # date
    r += bytes([flags])
    r += b"\x00"          # unit size
    r += b"\x00"          # gap size
    r += bytes([len(name)])
    r += name
    if len(name) % 2 == 0:
        r += b"\x00"      # pad to even
    r[0] = len(r)
    return bytes(r)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    nsec = 23
    img = bytearray(nsec * SECTOR)

    # --- LBA 16: PVD ---
    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:8 + 11] = fix(b"KAM_TEST_ISO", 11)
    pvd[80:88] = both32(nsec)
    pvd[156:156 + 34] = fix(dir_record(20, SECTOR, 2, b"\x00"), 34)
    img[16 * SECTOR:17 * SECTOR] = pvd

    # --- LBA 17: Boot Record ---
    br = bytearray(SECTOR)
    br[0] = 0
    br[1:6] = b"CD001"
    br[6] = 1
    br[7:7 + 23] = fix(b"EL TORITO SPECIFICATION", 23)
    struct.pack_into("<I", br, 71, 19)
    img[17 * SECTOR:18 * SECTOR] = br

    # --- LBA 18: Terminator ---
    img[18 * SECTOR] = 255
    img[18 * SECTOR + 1:18 * SECTOR + 6] = b"CD001"

    # --- LBA 19: El Torito catalog ---
    cat = bytearray(SECTOR)
    cat[0] = 1            # validation entry
    cat[1] = 0            # x86
    cat[4:4 + 16] = fix(b"KAM TEST CATALOG", 16)
    struct.pack_into("<H", cat, 30, 0xAA55)
    words = struct.unpack("<16H", cat[0:32])
    cksum = (-sum(words)) & 0xFFFF
    struct.pack_into("<H", cat, 28, cksum)
    cat[32] = 0x88        # initial entry, bootable
    cat[33] = 0           # no emulation
    struct.pack_into("<H", cat, 32 + 6, 1)    # sector count
    struct.pack_into("<I", cat, 32 + 8, 21)   # load RBA
    img[19 * SECTOR:20 * SECTOR] = cat

    # --- LBA 20: root directory ---
    root = bytearray(SECTOR)
    r1 = dir_record(20, SECTOR, 2, b"\x00")
    r2 = dir_record(20, SECTOR, 2, b"\x01")
    r3 = dir_record(22, 14, 0, b"HELLO.TXT;1")
    root[0:len(r1)] = r1
    root[len(r1):len(r1) + len(r2)] = r2
    root[len(r1) + len(r2):len(r1) + len(r2) + len(r3)] = r3
    img[20 * SECTOR:21 * SECTOR] = root

    # --- LBA 21: boot image ---
    img[21 * SECTOR:21 * SECTOR + 12] = b"KAM-BOOT-IMG"

    # --- LBA 22: file data ---
    img[22 * SECTOR:22 * SECTOR + 14] = b"hello from iso"

    with open(a.out, "wb") as f:
        f.write(bytes(img))
    assert len(img) == nsec * SECTOR, len(img)
    print(f"mkiso: {a.out} ({len(img)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

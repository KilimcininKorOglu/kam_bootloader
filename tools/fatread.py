#!/usr/bin/env python3
"""fatread.py: read a file out of a mkesp FAT16 image (host-side check for
the BOOT.LOG the firmware writes). LFN entries are skipped; the 8.3 entry
carries the cluster chain.
Usage: fatread.py IMAGE FATPATH   (e.g. fatread.py esp.img KAM/BOOT.LOG)
"""
import struct
import sys

SECTOR = 512


def main():
    img_path, fatpath = sys.argv[1], sys.argv[2].upper().split("/")
    d = open(img_path, "rb").read()

    if d[510:512] == b"\x55\xAA" and d[446:446 + 48] != bytes(48):
        start = struct.unpack_from("<I", d, 446 + 8)[0]
        base = start * SECTOR
    else:
        base = 0
    v = d[base:base + SECTOR]
    spc = v[13]
    reserved = struct.unpack_from("<H", v, 14)[0]
    fats = v[16]
    nroot = struct.unpack_from("<H", v, 17)[0]
    spf = struct.unpack_from("<H", v, 22)[0]
    root_sec = nroot * 32 // SECTOR
    first_data = reserved + fats * spf + root_sec
    fat0 = base + reserved * SECTOR

    def cluster_off(cl):
        return base + (first_data + (cl - 2) * spc) * SECTOR

    def find(fatdir, want, isdir):
        o = 0
        while fatdir[o] != 0x00:
            if fatdir[o] != 0xE5 and fatdir[o + 11] != 0x0F:
                name = fatdir[o:o + 11]
                base8 = name[:8].decode().rstrip()
                ext = name[8:].decode().rstrip()
                full = base8 + ("." + ext if ext else "")
                if full == want and bool(fatdir[o + 11] & 0x10) == isdir:
                    cl = struct.unpack_from("<H", fatdir, o + 26)[0]
                    sz = struct.unpack_from("<I", fatdir, o + 28)[0]
                    return cl, sz
            o += 32
        return None

    cur = d[base + (first_data - root_sec) * SECTOR:
            base + first_data * SECTOR]
    for i, part in enumerate(fatpath):
        last = i == len(fatpath) - 1
        hit = find(cur, part, not last)
        if hit is None:
            print(f"fatread: not found: {part}", file=sys.stderr)
            return 1
        cl, sz = hit
        if last:
            out = bytearray()
            while True:
                off = cluster_off(cl)
                out += d[off:off + spc * SECTOR]
                nxt = struct.unpack_from("<H", d, fat0 + cl * 2)[0]
                if nxt >= 0xFFF8:
                    break
                cl = nxt
            sys.stdout.buffer.write(bytes(out[:sz]))
            return 0
        cur = bytearray()
        while True:
            off = cluster_off(cl)
            cur += d[off:off + spc * SECTOR]
            nxt = struct.unpack_from("<H", d, fat0 + cl * 2)[0]
            if nxt >= 0xFFF8:
                break
            cl = nxt


if __name__ == "__main__":
    sys.exit(main())

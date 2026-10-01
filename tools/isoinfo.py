#!/usr/bin/env python3
"""isoinfo.py: host-side ISO9660 inspector. Lists the volume label, walks
the root directory tree, and hashes file contents. No dependencies.
Usage: isoinfo.py IMAGE [--sha256]
"""
import argparse
import hashlib
import struct
import sys

SECTOR = 2048


def both(v, o, n=4):
    return int.from_bytes(v[o:o + n], "little")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--sha256", action="store_true")
    a = ap.parse_args()
    d = open(a.image, "rb").read()

    pvd = d[16 * SECTOR:17 * SECTOR]
    if pvd[0] != 1 or pvd[1:6] != b"CD001":
        print("isoinfo: no primary volume descriptor", file=sys.stderr)
        return 1
    print(f"label: {pvd[40:72].split(bytes([0]))[0].decode()}")
    print(f"sectors: {both(pvd, 80)}")
    root_lba = both(pvd, 156 + 2)
    root_size = both(pvd, 156 + 10)
    nfiles = 0

    def walk(lba, size, prefix):
        nonlocal nfiles
        off = 0
        while off + 30 <= size:
            r = d[lba * SECTOR + off:]
            ln = r[0]
            if ln == 0:
                off = (off // SECTOR + 1) * SECTOR
                continue
            if ln < 30 or off + ln > size:
                break
            if ln == 30:  # dot records carry a 1-byte 0x00/0x01 name
                off += ln
                continue
            if ln < 34:
                break
            flags = r[25]
            nlen = r[28]
            ext = both(r, 2)
            sz = both(r, 10)
            name = r[29:29 + nlen].decode("latin1")
            if flags & 2:
                walk(ext, sz, prefix + name.split(";")[0] + "/")
            else:
                short = name.split(";")[0]
                line = f"{prefix}{short} lba={ext} size={sz}"
                if a.sha256:
                    h = hashlib.sha256(d[ext * SECTOR:ext * SECTOR + sz])
                    line += f" sha256={h.hexdigest()}"
                print(line)
                nfiles += 1
            off += ln

    walk(root_lba, min(root_size, 8 * SECTOR), "")
    print(f"files: {nfiles}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

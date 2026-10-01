#!/usr/bin/env python3
"""imgcheck.py: structural audit for mkesp FAT images. Re-derives every
offset from the BPB/MBR instead of trusting the builder, and fails on:
bad signatures, FAT16-range violations, broken LFN checksums, unterminated
or shared cluster chains, missing expected files.
Usage: imgcheck.py IMG [--expect FATPATH ...]
"""
import struct
import sys

SECTOR = 512


def fail(msg):
    print(f"imgcheck FAIL: {msg}")
    return 1


def lfn_checksum(dos11):
    s = 0
    for b in dos11:
        s = ((s >> 1) + ((s & 1) << 7) + b) & 0xFF
    return s


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--expect")]
    expects = []
    for i, a in enumerate(sys.argv[1:]):
        if a == "--expect":
            expects = sys.argv[1:][i + 1:]
            break
    if not args:
        print("usage: imgcheck.py IMG [--expect FATPATH ...]")
        return 2
    d = open(args[0], "rb").read()

    # --- MBR or superfloppy ---
    if d[510:512] == b"\x55\xAA" and d[446:446 + 48] != bytes(48):
        ptype = d[446 + 4]
        if ptype not in (0x0E, 0x06, 0x0C):
            return fail(f"partition type {ptype:#x}")
        start = struct.unpack_from("<I", d, 446 + 8)[0]
        total = struct.unpack_from("<I", d, 446 + 12)[0]
        base = start * SECTOR
        if base + total * SECTOR > len(d):
            return fail("partition exceeds image")
    else:
        base = 0
        total = len(d) // SECTOR

    v = d[base:base + SECTOR]
    if v[510:512] != b"\x55\xAA":
        return fail("volume boot signature")
    bps = struct.unpack_from("<H", v, 11)[0]
    spc = v[13]
    reserved = struct.unpack_from("<H", v, 14)[0]
    fats = v[16]
    nroot = struct.unpack_from("<H", v, 17)[0]
    spf = struct.unpack_from("<H", v, 22)[0]
    if bps != SECTOR or fats != 2 or nroot == 0 or spf == 0:
        return fail(f"BPB odd: bps={bps} fats={fats} roots={nroot} spf={spf}")
    root_sec = nroot * 32 // SECTOR
    first_data = reserved + fats * spf + root_sec
    ncl = (total - first_data) // spc
    if not 4085 <= ncl <= 65525:
        return fail(f"cluster count {ncl} outside FAT16 range")
    label = v[54:62]
    if label not in (b"FAT16   ",):
        return fail(f"unexpected type label {label!r}")

    fat0 = base + reserved * SECTOR
    if d[fat0:fat0 + 3] != b"\xF8\xFF\xFF":
        return fail("FAT media descriptor")

    def fat_get(cl):
        return struct.unpack_from("<H", d, fat0 + cl * 2)[0]

    def cluster_off(cl):
        return base + (first_data + (cl - 2) * spc) * SECTOR

    # --- walk directories ---
    seen_clusters = {}
    found = []

    def dos83(name11):
        base = name11[:8].decode("ascii", "replace").rstrip()
        ext = name11[8:].decode("ascii", "replace").rstrip()
        return base + ("." + ext if ext else "")

    def check_chain(cl, what):
        n = 0
        while True:
            if cl < 2 or cl >= ncl + 2:
                return fail(f"{what}: cluster {cl} out of range")
            if cl in seen_clusters:
                return fail(f"{what}: cluster {cl} shared "
                            f"with {seen_clusters[cl]}")
            seen_clusters[cl] = what
            nxt = fat_get(cl)
            n += 1
            if n > ncl + 1:
                return fail(f"{what}: chain loop")
            if nxt >= 0xFFF8:
                return None
            cl = nxt

    def walk(dir_off, nentries, prefix):
        pending_lfn = []
        for i in range(nentries):
            o = dir_off + i * 32
            e = d[o:o + 32]
            if e[0] == 0x00:
                break
            if e[0] == 0xE5:
                pending_lfn = []
                continue
            if e[11] == 0x0F:
                pending_lfn.append(bytes(e))
                continue
            name11 = bytes(e[0:11])
            if pending_lfn:
                blob = b"".join(
                    x[1:11] + x[14:26] + x[28:32] for x in pending_lfn)
                units = struct.unpack(f"<{len(blob)//2}H", blob)
                seq_ok = all(
                    (pending_lfn[k][0] & 0x1F) == len(pending_lfn) - k
                    for k in range(len(pending_lfn)))
                last_ok = bool(pending_lfn[0][0] & 0x40)
                cks = [x[13] for x in pending_lfn]
                if not (seq_ok and last_ok and
                        all(c == lfn_checksum(name11) for c in cks)):
                    return fail(f"LFN chain broken before {name11!r}")
                if 0x0000 not in units and 0xFFFF not in units:
                    return fail(f"LFN unterminated before {name11!r}")
                pending_lfn = []
            attr = e[11]
            cl = struct.unpack_from("<H", e, 26)[0]
            sz = struct.unpack_from("<I", e, 28)[0]
            disp = dos83(name11)
            if attr & 0x10:
                if prefix is not None:
                    r = check_chain(cl, prefix + disp)
                    if r:
                        return r
                    walk(cluster_off(cl), spc * SECTOR // 32,
                         prefix + disp + "/")
            else:
                if prefix is not None:
                    found.append(prefix + disp)
                r = check_chain(cl, disp)
                if r:
                    return r
        return None

    r = walk(base + first_data * SECTOR - root_sec * SECTOR, nroot, "")
    if r:
        return r
    for want in expects:
        if want not in found and want.upper() not in [
                f.upper() for f in found]:
            return fail(f"expected file missing: {want} "
                        f"(have {[f for f in found][:8]})")
    print(f"imgcheck: OK ({len(found)} files, {len(seen_clusters)} "
          f"clusters, FAT16 x{ncl})")
    return 0


if __name__ == "__main__":
    sys.exit(main())

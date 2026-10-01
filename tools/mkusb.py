#!/usr/bin/env python3
"""mkusb.py: build a bootable USB stick image for KAM.
Layout: MBR (our boot code + partition table) + BIOS payload in the
alignment gap + ESP volume in partition 1.

  LBA 0:     MBR: first 446 bytes of mbr.bin + partition table + 0xAA55
  LBA 1-16:  stage2.bin (BIOS loads it from absolute LBAs, as before)
  LBA 17+:   kernel file (absolute LBAs, as before)
  LBA 2048:  ESP FAT volume (partition 1, bootable, type 0x0E)

The BIOS path is unchanged (gap LBAs == floppy LBAs); UEFI boots the ESP
partition. FAT structures never overlap the BIOS payload.
Usage: mkusb.py --mbr mbr.bin --stage2 stage2.bin --kernel kernel.elf
                 --esp esp.img --out usb.img
"""
import argparse
import os
import struct
import sys

SECTOR = 512
PART_START = 2048
MBR_CODE = 446


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mbr", required=True)
    ap.add_argument("--stage2", required=True)
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--esp", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    with open(a.mbr, "rb") as f:
        mbr = f.read()
    with open(a.stage2, "rb") as f:
        stage2 = f.read()
    with open(a.kernel, "rb") as f:
        kernel = f.read()
    with open(a.esp, "rb") as f:
        esp = f.read()

    if len(mbr) != SECTOR or mbr[510:512] != b"\x55\xAA":
        print("mkusb: mbr must be a 512B signed sector", file=sys.stderr)
        return 1
    code_end = next((i for i in range(MBR_CODE - 1, -1, -1)
                     if mbr[i] != 0), -1) + 1
    if code_end > MBR_CODE:
        print("mkusb: mbr code exceeds 446 bytes", file=sys.stderr)
        return 1
    if len(stage2) > 16 * SECTOR:
        print("mkusb: stage2 must fit 16 sectors", file=sys.stderr)
        return 1
    if len(kernel) > 128 * SECTOR:
        print("mkusb: kernel must fit 128 sectors", file=sys.stderr)
        return 1
    if len(esp) % SECTOR != 0:
        print("mkusb: esp must be sector-aligned", file=sys.stderr)
        return 1
    esp_sectors = len(esp) // SECTOR

    total = PART_START + esp_sectors
    img = bytearray(total * SECTOR)
    mbr_out = bytearray(SECTOR)
    mbr_out[0:MBR_CODE] = mbr[0:MBR_CODE]
    p = 446
    mbr_out[p + 0] = 0x80
    mbr_out[p + 1:p + 4] = b"\xFF\xFF\xFF"
    mbr_out[p + 4] = 0x0E
    mbr_out[p + 5:p + 8] = b"\xFF\xFF\xFF"
    struct.pack_into("<I", mbr_out, p + 8, PART_START)
    struct.pack_into("<I", mbr_out, p + 12, esp_sectors)
    mbr_out[510:512] = b"\x55\xAA"
    img[0:SECTOR] = mbr_out
    img[1 * SECTOR:1 * SECTOR + len(stage2)] = stage2
    img[17 * SECTOR:17 * SECTOR + len(kernel)] = kernel
    img[PART_START * SECTOR:] = esp

    os.makedirs(os.path.dirname(a.out) or ".", exist_ok=True)
    with open(a.out, "wb") as f:
        f.write(bytes(img))
    print(f"mkusb: {a.out} ({len(img)//1024//1024}MB, "
          f"mbr code {code_end}B)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

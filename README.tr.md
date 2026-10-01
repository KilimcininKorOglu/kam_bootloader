# KAM Bootloader

C ile yazilmis x86 + ARM64 bootloader.

- Hedef: UEFI (x86_64 + AArch64) + MBR/BIOS (x86, 16-bit real mode).
- Not: MBR/BIOS yalnizca x86 dunyasinda vardir. ARM64 tarafi UEFI-only'dir.

## Yerlesim

```
include/kam/   -> basliklar (types, efi, bios, console)
src/uefi/      -> UEFI uygulamasi (x86_64 + AArch64, tek C kaynagi)
src/bios/      -> MBR boot sector (nasm, 512 byte) + stage2 taslagi
linker/        -> UEFI linker scriptleri
tools/         -> ESP imaj scripti + QEMU kosturma scriptleri
```

## Hizli baslangic (macOS)

```sh
brew install nasm mtools qemu llvm
make bios        # build/mbr.bin (512 byte, 0xAA55 imzali)
make check-uefi  # UEFI C kodunu syntax-check eder (link icin lld gerekir)
```

Gerceke `.efi` link icin `ld.lld` gerekir. Yoksa:

```sh
brew install lld
make uefi-x64    # build/BOOTX64.EFI
make uefi-aa64   # build/BOOTAA64.EFI
make esp         # build/esp.img (icinde EFI/BOOT/*.EFI olan FAT)
make run-x64     # QEMU + OVMF x86_64
make run-aa64    # QEMU + EDK2 AArch64
make run-bios    # QEMU SeaBIOS (MBR)
```

## Ilkeler

1. Her arch icin ayri entry, ortak C cekirdek.
2. Kucuk binary. Boot I/O-bound'dur: hiz diskte + firmware'dedir.

# KAM Bootloader

An x86 + ARM64 bootloader written in C.

- Goal: UEFI (x86_64 + AArch64) + MBR/BIOS (x86, 16-bit real mode).
- Note: MBR/BIOS only exists in the x86 world. The ARM64 side is UEFI-only.

## Layout

```
include/kam/   -> headers (types, efi, bios, console)
src/uefi/      -> UEFI application (x86_64 + AArch64, single C source)
src/bios/      -> MBR boot sector (nasm, 512 bytes) + stage2 draft
linker/        -> UEFI linker scripts
tools/         -> ESP image script + QEMU run scripts
```

## Quick start (macOS)

```sh
brew install nasm mtools qemu llvm
make bios        # build/mbr.bin (512 bytes, 0xAA55 signature)
make check-uefi  # syntax-checks the UEFI C code (lld needed for linking)
```

A real `.efi` link requires `ld.lld`. If missing:

```sh
brew install lld
make uefi-x64    # build/BOOTX64.EFI
make uefi-aa64   # build/BOOTAA64.EFI
make esp         # build/esp.img (FAT, containing EFI/BOOT/*.EFI)
make run-x64     # QEMU + OVMF x86_64
make run-aa64    # QEMU + EDK2 AArch64
make run-bios    # QEMU SeaBIOS (MBR)
```

## Principles

1. Separate entry per arch, shared C core.
2. Small binaries. Boot is I/O-bound: speed lives in disk + firmware.

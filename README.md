# KAM Bootloader

An x86 + ARM64 bootloader written in C.

- Paths: UEFI (x86_64 + AArch64) + BIOS (x86 MBR + stage2 -> long mode).
- ARM64 is UEFI-only; there is no MBR/BIOS outside x86.

## What it boots

- ELF64 kernels with a shared prepare/commit loader (BIOS + UEFI).
- Third-party `.EFI` via `LoadImage`/`StartImage` chainload.
- `bzImage` direct boot (setup probe, initrd, cmdline, handover entry).
- ISO9660 + El Torito probe, Windows layout detection, GOP header art.
- Static `KAM/KAM.INI` config (labels, timeout, default, salted SHA-256
  boot password) merged with dynamic multi-volume scan.

## Layout

```
include/kam/   -> headers (UEFI, BIOS, ELF, ISO, config, crypto)
src/uefi/      -> UEFI app, both arches from one C source
src/bios/      -> MBR (512B) + 16-to-64 stage2 trampoline + C payload
src/kernel/    -> test kernel stub (both arches)
src/linux/     -> bzImage test fixture (both arches)
linker/        -> per-arch linker scripts
tools/         -> mkesp/mkiso/mkusb/mkpasswd/isoinfo/imgcheck/fatread,
                   QEMU drivers (drive_boot, shot_boot)
tests/         -> host unit tests (no firmware needed)
```

## Quick start (macOS)

```sh
brew install nasm lld llvm qemu
make test-all   # full matrix: BIOS + UEFI x64/aa64 + media + host checks
```

Single targets: `make test-bios`, `make test-x64`, `make test-aa64`,
`make test-cd-bios`, `make test-cd-efi`, `make test-usb-bios`,
`make test-usb-efi`, `make unittest`, `make check-msvc`.

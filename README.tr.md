# KAM Bootloader

C ile yazilmis x86 + ARM64 bootloader.

- Yollar: UEFI (x86_64 + AArch64) + BIOS (x86 MBR + stage2 -> long mode).
- ARM64 UEFI-only'dir; x86 disinda MBR/BIOS yoktur.

## Ne boot eder

- Ortak prepare/commit loader ile ELF64 kernel'lar (BIOS + UEFI).
- `LoadImage`/`StartImage` ile ucuncu parti `.EFI` chainload.
- Direkt bzImage boot (setup probe, initrd, cmdline, handover entry).
- ISO9660 + El Torito probe, Windows yerlesim tespiti, GOP baslik resmi.
- Statik `KAM/KAM.INI` config (label, timeout, default, salted SHA-256
  boot sifresi) + dinamik cok-volum tarama ile birlesir.

## Yerlesim

```
include/kam/   -> basliklar (UEFI, BIOS, ELF, ISO, config, crypto)
src/uefi/      -> UEFI app, iki arch tek C kaynak
src/bios/      -> MBR (512B) + 16-to-64 stage2 trampoline + C payload
src/kernel/    -> test kernel stub (iki arch)
src/linux/     -> bzImage test fixture (iki arch)
linker/        -> arch basi linker scriptler
tools/         -> mkesp/mkiso/mkusb/mkpasswd/isoinfo/imgcheck/fatread,
                   QEMU suruculer (drive_boot, shot_boot)
tests/         -> host unit testler (firmware gerekmez)
```

## Hizli baslangic (macOS)

```sh
brew install nasm lld llvm qemu
make test-all   # tam matris: BIOS + UEFI x64/aa64 + medya + host kontroller
```

Tekil hedefler: `make test-bios`, `make test-x64`, `make test-aa64`,
`make test-cd-bios`, `make test-cd-efi`, `make test-usb-bios`,
`make test-usb-efi`, `make unittest`, `make check-msvc`.

## Ilkeler

1. Her arch icin ayri entry, ortak C cekirdek.
2. Kucuk binary. Boot I/O-bound'dur: hiz diskte + firmware'dedir.
3. Testi gecmeyen faz bitmis sayilmaz (QEMU veya host).

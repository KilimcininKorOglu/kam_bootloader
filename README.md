# KAM Bootloader

An x86 + ARM64 bootloader written in C. Freestanding, no libc: one UEFI
C source builds for both x86_64 and AArch64, plus a BIOS path
(512-byte MBR + stage2 trampoline + C payload) for x86.

- UEFI: x86_64 + AArch64 from a single source.
- BIOS: x86 MBR + stage2 (16-bit real mode to 64-bit long mode).
- ARM64 is UEFI-only; there is no MBR/BIOS outside x86.

## What it boots

- ELF64 kernels through a shared prepare/commit loader (BIOS + UEFI),
  with a fixed test kernel stub proving load, params and jump.
- Third-party `.EFI` binaries via `LoadImage`/`StartImage` chainload.
- `bzImage` direct boot: setup probe, initrd, cmdline, params page
  with e820/cmdline/ramdisk at genuine offsets, handover entry.
- ISO9660 + El Torito probe, Windows layout detection, GOP header art.
- Static `KAM/KAM.INI` config (labels, timeout, default, salted SHA-256
  boot password, per-entry initrd/cmdline) merged with a dynamic
  multi-volume scan (own ESP, other disks, data partitions; dedupe
  by path).
- An in-memory boot log mirrored to ConOut and flushed to
  `KAM/BOOT.LOG`. The password gate logs only outcomes, never secrets.

## Layout

```
include/kam/   -> headers (UEFI, BIOS, ELF, ISO, config, crypto)
src/uefi/      -> UEFI app, both arches from one C source
src/bios/      -> MBR (512B) + 16-to-64 stage2 trampoline + C payload
src/kernel/    -> test kernel stub (both arches)
src/linux/     -> bzImage test fixture (both arches)
linker/        -> per-arch linker scripts
tools/         -> image builders (mkesp/mkiso/mkusb/mkpasswd),
                  inspectors (isoinfo/imgcheck/fatread),
                  QEMU drivers (drive_boot, shot_boot)
tests/         -> host unit tests (no firmware needed)
.github/       -> CI workflow (full matrix on ubuntu-latest)
```

## Requirements

macOS:

```sh
brew install nasm lld llvm qemu
```

Linux (same as CI):

```sh
sudo apt-get install -y nasm llvm clang lld \
  qemu-system-x86 qemu-system-arm ovmf qemu-efi-aarch64
```

On Linux the Makefile defaults (Homebrew paths) are overridden:

```sh
make test-all LLVM=/usr/bin CLANG_CL=/usr/bin/clang-cl \
  QEMU_X64_FW=/usr/share/OVMF/OVMF_CODE_4M.fd \
  QEMU_AA64_FW=/usr/share/AAVMF/AAVMF_CODE.fd
```

## Quick start

```sh
make test-all   # full matrix: BIOS + UEFI x64/aa64 + media + host checks
```

Interactive runs (no assertions, watch the console yourself):

```sh
make run-bios
make run-x64
make run-aa64
```

## Test matrix

`make test-all` runs everything below. QEMU tests boot real firmware
(SeaBIOS/OVMF on x64, EDK2 virt on aa64) and assert serial output;
`shot_boot` additionally screenshots the display and asserts GOP pixels.

| Target(s) | What is covered |
|---|---|
| `test-bios` | MBR, stage2 long-mode entry, E820 map, ELF kernel |
| `unittest` | Host tests: SHA-256 vectors, ELF prepare/commit, config parsing, ISO probe, plus `mkesp`/`imgcheck`/`isoinfo` image audits |
| `check-msvc` | clang-cl compile check on x64 + aa64 sources |
| `test-x64`, `test-aa64` | Default boot reaches the test kernel |
| `test-chain-x64`, `test-chain-aa64` | `.EFI` chainload via LoadImage/StartImage |
| `test-iso-x64`, `test-iso-aa64` | ISO9660 + El Torito probe |
| `test-config-x64`, `test-config-aa64` | `KAM.INI` labels, timeout, default entry |
| `test-win-x64` | Windows layout detection |
| `test-linux-x64`, `test-linux-aa64` | bzImage direct boot |
| `test-gop-x64`, `test-gop-aa64` | GOP header art, resolution line, pixel asserts |
| `test-cd-bios`, `test-cd-efi` | El Torito CD boot, BIOS and UEFI |
| `test-usb-bios`, `test-usb-efi` | USB stick image boot, BIOS and UEFI |
| `test-multivol-x64` | Boot entries from a second disk |
| `test-parts-x64` | Boot entries from a data partition |
| `test-pwd-x64`, `test-pwd-aa64` | Correct password boots |
| `test-pwddeny-x64`, `test-pwddeny-aa64` | Wrong password is denied |

## Configuration (`KAM/KAM.INI`)

Line-based, no allocation, unknown sections/keys are ignored:

```ini
timeout 5
default 1

[kernel]
label My Kernel
path \KAM\KERNEL.ELF

[chain]
label Hello
path \KAM\HELLO.EFI

[iso]
label Test ISO
path \KAM\TEST.ISO

[linux]
label Test Linux
path \KAM\VMLINUZ
initrd \KAM\INITRD.IMG
cmdline kam-test console=ttyS0

password_salt COST164
password_hash <64 hex chars of SHA-256(salt + password)>
```

Generate the password lines with:

```sh
python3 tools/mkpasswd.py --password kamboot --salt COST164 --timeout 5
```

Only the salted hash is stored; the password itself is never written
anywhere.

## Image builders

- `tools/mkesp.py`: partitioned ESP disk image (bootable FAT16 ESP,
  optional second data partition, or partitionless superfloppy for
  El Torito EFI boot images). Pure Python, no mtools.
- `tools/mkiso.py`: minimal ISO9660 image with El Torito catalog,
  carrying both a BIOS boot path and an EFI boot image.
- `tools/mkusb.py`: USB stick image: MBR + BIOS payload in the
  alignment gap + ESP volume in partition 1, so one image boots
  both BIOS and UEFI.
- `tools/isoinfo.py`, `tools/imgcheck.py`, `tools/fatread.py`:
  host-side inspectors used by the tests (ISO listing, FAT structural
  audit, file extraction).

## CI

`.github/workflows/ci.yaml` installs the Linux dependencies above and
runs `make test-all` on `ubuntu-latest` for every push and pull request.

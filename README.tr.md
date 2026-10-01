# KAM Bootloader

C ile yazılmış x86 + ARM64 önyükleyici. Bağımsız (freestanding)
çalışır, libc gerektirmez: tek bir UEFI C kaynağı hem x86_64 hem de
AArch64 için derlenir; x86 için ek olarak bir BIOS yolu vardır
(512 baytlık MBR + stage2 sıçrama kodu + C yükü).

- UEFI: tek kaynaktan x86_64 + AArch64.
- BIOS: x86 MBR + stage2 (16 bit gerçek kipinden 64 bit uzun kipe geçiş).
- ARM64 yalnızca UEFI ile açılır; x86 dışında MBR/BIOS yoktur.

## Neleri başlatır

- BIOS ve UEFI'nin ortak kullandığı prepare/commit yükleyici ile ELF64
  çekirdekler; yükleme, parametre ve atlamayı kanıtlayan sabit bir test
  çekirdeğiyle birlikte.
- `LoadImage`/`StartImage` ile üçüncü parti `.EFI` dosyaları (chainload).
- Doğrudan `bzImage` başlatma: setup denetimi, initrd, komut satırı,
  e820/komut satırı/ramdisk alanlarını gerçek ofsetlerde taşıyan
  parametre sayfası ve devir (handover) girişi.
- ISO9660 + El Torito denetimi, Windows yerleşim tespiti, GOP başlık
  resmi.
- Statik `KAM/KAM.INI` yapılandırması (etiketler, zaman aşımı, varsayılan
  girdi, tuzlanmış SHA-256 açılış parolası, girdi başına initrd/komut
  satırı) ile dinamik çok birimli taramanın birleşimi (kendi ESP'si,
  diğer diskler, veri bölümleri; yola göre tekilleştirme).
- Bellek içi açılış günlüğü ConOut'a yansıtılır ve `KAM/BOOT.LOG`
  dosyasına yazılır. Parola denetimi yalnızca sonucu günlüğe yazar,
  parolanın kendisini asla yazmaz.

## Dizin yapısı

```
include/kam/   -> başlıklar (UEFI, BIOS, ELF, ISO, yapılandırma, kripto)
src/uefi/      -> UEFI uygulaması, iki mimari tek C kaynak
src/bios/      -> MBR (512B) + 16'dan 64 bite stage2 sıçrama kodu + C yükü
src/kernel/    -> test çekirdeği (iki mimari)
src/linux/     -> bzImage test düzeneği (iki mimari)
linker/        -> mimari başına bağlayıcı betikleri
tools/         -> imaj üreticiler (mkesp/mkiso/mkusb/mkpasswd),
                  inceleyiciler (isoinfo/imgcheck/fatread),
                  QEMU sürücüleri (drive_boot, shot_boot)
tests/         -> host birim testleri (firmware gerekmez)
.github/       -> CI iş akışı (ubuntu-latest üzerinde tam matris)
```

## Gereksinimler

macOS:

```sh
brew install nasm lld llvm qemu
```

Linux (CI ile aynı):

```sh
sudo apt-get install -y nasm llvm clang lld \
  qemu-system-x86 qemu-system-arm ovmf qemu-efi-aarch64
```

Linux'ta Makefile varsayılanları (Homebrew yolları) şöyle geçersiz
kılınır:

```sh
make test-all LLVM=/usr/bin CLANG_CL=/usr/bin/clang-cl \
  QEMU_X64_FW=/usr/share/OVMF/OVMF_CODE_4M.fd \
  QEMU_AA64_FW=/usr/share/AAVMF/AAVMF_CODE.fd
```

## Hızlı başlangıç

```sh
make test-all   # tam matris: BIOS + UEFI x64/aa64 + ortam + host denetimleri
```

Etkileşimli çalıştırma (doğrulama yapmaz, konsolu kendiniz izlersiniz):

```sh
make run-bios
make run-x64
make run-aa64
```

## Test matrisi

`make test-all` aşağıdakilerin tamamını çalıştırır. QEMU testleri
gerçek firmware'i başlatır (x64'te SeaBIOS/OVMF, aa64'te EDK2 virt) ve
seri çıktıyı doğrular; `shot_boot` ayrıca ekran görüntüsü alıp GOP
piksellerini doğrular.

| Hedef(ler) | Kapsam |
|---|---|
| `test-bios` | MBR, stage2 uzun kip girişi, E820 haritası, ELF çekirdek |
| `unittest` | Host testleri: SHA-256 vektörleri, ELF prepare/commit, yapılandırma ayrıştırma, ISO denetimi; ayrıca `mkesp`/`imgcheck`/`isoinfo` imaj denetimleri |
| `check-msvc` | x64 + aa64 kaynakları için clang-cl derleme denetimi |
| `test-x64`, `test-aa64` | Varsayılan açılış test çekirdeğine ulaşır |
| `test-chain-x64`, `test-chain-aa64` | LoadImage/StartImage ile `.EFI` chainload |
| `test-iso-x64`, `test-iso-aa64` | ISO9660 + El Torito denetimi |
| `test-config-x64`, `test-config-aa64` | `KAM.INI` etiketleri, zaman aşımı, varsayılan girdi |
| `test-win-x64` | Windows yerleşim tespiti |
| `test-linux-x64`, `test-linux-aa64` | Doğrudan bzImage başlatma |
| `test-gop-x64`, `test-gop-aa64` | GOP başlık resmi, çözünürlük satırı, piksel doğrulama |
| `test-cd-bios`, `test-cd-efi` | El Torito CD açılışı, BIOS ve UEFI |
| `test-usb-bios`, `test-usb-efi` | USB bellek imajı açılışı, BIOS ve UEFI |
| `test-multivol-x64` | İkinci diskten açılış girdileri |
| `test-parts-x64` | Veri bölümünden açılış girdileri |
| `test-pwd-x64`, `test-pwd-aa64` | Doğru parola açılır |
| `test-pwddeny-x64`, `test-pwddeny-aa64` | Yanlış parola reddedilir |

## Yapılandırma (`KAM/KAM.INI`)

Satır tabanlıdır, bellek ayırmaz; bilinmeyen bölümler ve anahtarlar
yok sayılır:

```ini
timeout 5
default 1

[kernel]
label Çekirdeğim
path \KAM\KERNEL.ELF

[chain]
label Merhaba
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
password_hash <SHA-256(salt + parola) değerinin 64 hex hanesi>
```

Parola satırlarını şununla üretin:

```sh
python3 tools/mkpasswd.py --password kamboot --salt COST164 --timeout 5
```

Yalnızca tuzlanmış özet saklanır; parolanın kendisi hiçbir yere
yazılmaz.

## İmaj üreticiler

- `tools/mkesp.py`: bölümlü ESP disk imajı (açılabilir FAT16 ESP,
  isteğe bağlı ikinci veri bölümü ya da El Torito EFI açılış imajları
  için bölümsüz superfloppy). Salt Python'dur, mtools gerekmez.
- `tools/mkiso.py`: El Torito kataloğu taşıyan en küçük ISO9660 imajı;
  hem BIOS hem EFI açılış yolunu içerir.
- `tools/mkusb.py`: USB bellek imajı: MBR + hizalama boşluğunda BIOS
  yükü + 1. bölümde ESP birimi; tek imaj hem BIOS hem UEFI ile açılır.
- `tools/isoinfo.py`, `tools/imgcheck.py`, `tools/fatread.py`:
  testlerin kullandığı host tarafı inceleyiciler (ISO listeleme,
  FAT yapısal denetimi, dosya çıkarma).

## CI

`.github/workflows/ci.yaml`, yukarıdaki Linux bağımlılıklarını kurar ve
her push ve pull request'te `ubuntu-latest` üzerinde `make test-all`
çalıştırır.

# KAM Makefile
BUILD   := build
LLVM    := /opt/homebrew/opt/llvm/bin
CC_X64  := $(LLVM)/clang --target=x86_64-unknown-windows
CC_AA64 := $(LLVM)/clang --target=aarch64-unknown-windows
CFLAGS  := -nostdlib -ffreestanding -fno-stack-protector -fshort-wchar \
           -mno-red-zone -Wall -Wextra -O2 -Iinclude -c
QEMU_X64_FW := /opt/homebrew/share/qemu/edk2-x86_64-code.fd
QEMU_AA64_FW := /opt/homebrew/share/qemu/edk2-aarch64-code.fd
LLVM_OBJCOPY := $(LLVM)/llvm-objcopy

CC_BIOS  := $(LLVM)/clang --target=x86_64-unknown-linux-gnu
BCFLAGS  := -ffreestanding -nostdlib -fno-stack-protector \
            -fno-unwind-tables -fno-asynchronous-unwind-tables \
            -mno-red-zone -Wall -Wextra -O2 -Iinclude -c
BIOS_HEADERS := include/kam/types.h include/kam/memmap.h \
                include/kam/bios_addrs.h include/kam/bios_console.h \
                include/kam/elf.h
KAM_HEADERS := include/kam/efi.h include/kam/types.h include/kam/console.h \
               include/kam/memmap.h include/kam/elf.h include/kam/raw_serial.h \
               include/kam/scan.h include/kam/iso.h include/kam/config.h \
               include/kam/gop.h include/kam/linux.h include/kam/bzimage.h \
               include/kam/sha256.h include/kam/bootlog.h include/kam/cpu.h
KERN_HEADERS := include/kam/types.h include/kam/memmap.h \
                include/kam/elf.h include/kam/raw_serial.h \
                include/kam/bzimage.h

CC_X64_LNX  := $(LLVM)/clang --target=x86_64-unknown-linux-gnu
CC_AA64_LNX := $(LLVM)/clang --target=aarch64-unknown-linux-gnu
KCFLAGS  := -ffreestanding -nostdlib -fno-stack-protector \
            -fno-unwind-tables -fno-asynchronous-unwind-tables \
            -mno-red-zone -Wall -Wextra -O2 -Iinclude -c

.PHONY: all bios bios-img check-uefi uefi-x64 uefi-aa64 kernel-x64 kernel-aa64 hello-x64 hello-aa64 esp testiso run-x64 run-aa64 run-bios test-x64 test-aa64 test-chain-x64 test-chain-aa64 test-iso-x64 test-iso-aa64 test-config-x64 test-config-aa64 test-gop-x64 test-gop-aa64 test-win-x64 test-linux-x64 test-linux-aa64 test-cd-bios test-cd-efi test-usb-bios test-usb-efi test-multivol-x64 test-parts-x64 test-pwd-x64 test-pwddeny-x64 test-pwd-aa64 test-pwddeny-aa64 test-bios unittest check-msvc clean

all: bios-img check-uefi uefi-x64 uefi-aa64 kernel-x64 kernel-aa64 hello-x64 hello-aa64

bios: $(BUILD)/mbr.bin
	@echo "MBR: $< ($$(wc -c < $<) bytes)"

$(BUILD)/mbr.bin: src/bios/mbr.asm
	@mkdir -p $(BUILD)
	nasm -f bin $< -o $@
	@sz=$$(wc -c < $@); test "$$sz" -eq 512 || (echo "MBR must be 512 bytes: $$sz"; exit 1)
	@tail -c 2 $@ | od -An -tx1 | grep -q "55  aa" || (echo "MBR signature missing"; exit 1)

# Compiler present, code sane? (no linking needed)
check-uefi:
	@mkdir -p $(BUILD)
	$(LLVM)/clang -fsyntax-only -ffreestanding -fshort-wchar -Wall -Wextra \
	  -Iinclude --target=x86_64-unknown-windows src/uefi/kam.c && echo "UEFI x64 syntax OK"
	$(LLVM)/clang -fsyntax-only -ffreestanding -fshort-wchar -Wall -Wextra \
	  -Iinclude --target=aarch64-unknown-windows src/uefi/kam.c && echo "UEFI aa64 syntax OK"

# MSVC-compat check (clang-cl, compile only, objects discarded).
# stage2_main.c is x86-only by design, hence absent from the aa64 list.
CLANG_CL := /opt/homebrew/opt/llvm/bin/clang-cl
CL_X64 := $(CLANG_CL) /c /Iinclude --target=x86_64-windows
CL_AA64 := $(CLANG_CL) /c /Iinclude --target=aarch64-windows
CL_SRCS_X64 := src/uefi/kam.c src/uefi/scan.c src/uefi/iso.c \
               src/uefi/config.c src/uefi/gop.c src/uefi/linux.c \
               src/uefi/sha256.c src/uefi/bootlog.c src/uefi/hello.c \
               src/uefi/hello2.c src/uefi/hello3.c src/bios/stage2_main.c
CL_SRCS_AA64 := src/uefi/kam.c src/kernel/kam_kernel.c src/linux/vmlinuz.c

check-msvc:
	@mkdir -p $(BUILD)
	@for f in $(CL_SRCS_X64); do \
	  $(CL_X64) $$f /Fobuild/cltest.obj || exit 1; \
	done
	@for f in $(CL_SRCS_AA64); do \
	  $(CL_AA64) $$f /Fobuild/cltest.obj || exit 1; \
	done
	@rm -f $(BUILD)/cltest.obj
	@echo "MSVC-compat: clang-cl clean on x64 + aa64"

# A real .efi link needs ld.lld: brew install lld
LD_LLD := $(shell command -v ld.lld 2>/dev/null || echo $(LLVM)/ld.lld)

uefi-x64: $(BUILD)/BOOTX64.EFI
uefi-aa64: $(BUILD)/BOOTAA64.EFI

KAM_HEADERS := include/kam/efi.h include/kam/types.h include/kam/console.h

$(BUILD)/kam_x64.o: src/uefi/kam.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_X64) $(CFLAGS) src/uefi/kam.c -o $@

$(BUILD)/kam_aa64.o: src/uefi/kam.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_AA64) $(CFLAGS) src/uefi/kam.c -o $@

$(BUILD)/scan_x64.o: src/uefi/scan.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_X64) $(CFLAGS) src/uefi/scan.c -o $@

$(BUILD)/scan_aa64.o: src/uefi/scan.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_AA64) $(CFLAGS) src/uefi/scan.c -o $@

$(BUILD)/iso_x64.o: src/uefi/iso.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_X64) $(CFLAGS) src/uefi/iso.c -o $@

$(BUILD)/iso_aa64.o: src/uefi/iso.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_AA64) $(CFLAGS) src/uefi/iso.c -o $@

$(BUILD)/config_x64.o: src/uefi/config.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_X64) $(CFLAGS) src/uefi/config.c -o $@

$(BUILD)/config_aa64.o: src/uefi/config.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_AA64) $(CFLAGS) src/uefi/config.c -o $@

$(BUILD)/gop_x64.o: src/uefi/gop.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_X64) $(CFLAGS) src/uefi/gop.c -o $@

$(BUILD)/gop_aa64.o: src/uefi/gop.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_AA64) $(CFLAGS) src/uefi/gop.c -o $@

$(BUILD)/linux_x64.o: src/uefi/linux.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_X64) $(CFLAGS) src/uefi/linux.c -o $@

$(BUILD)/linux_aa64.o: src/uefi/linux.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_AA64) $(CFLAGS) src/uefi/linux.c -o $@

$(BUILD)/sha_x64.o: src/uefi/sha256.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_X64) $(CFLAGS) src/uefi/sha256.c -o $@

$(BUILD)/sha_aa64.o: src/uefi/sha256.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_AA64) $(CFLAGS) src/uefi/sha256.c -o $@

$(BUILD)/bootlog_x64.o: src/uefi/bootlog.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_X64) $(CFLAGS) src/uefi/bootlog.c -o $@

$(BUILD)/bootlog_aa64.o: src/uefi/bootlog.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_AA64) $(CFLAGS) src/uefi/bootlog.c -o $@

$(BUILD)/BOOTX64.EFI: $(BUILD)/kam_x64.o $(BUILD)/scan_x64.o $(BUILD)/iso_x64.o $(BUILD)/config_x64.o $(BUILD)/gop_x64.o $(BUILD)/linux_x64.o $(BUILD)/sha_x64.o $(BUILD)/bootlog_x64.o linker/uefi_x64.ld
	@test -x $(LD_LLD) || (echo "ld.lld missing: brew install lld"; exit 1)
	$(LD_LLD) -flavor link -subsystem:efi_application -entry:efi_main \
	  -out:$@ $(BUILD)/kam_x64.o $(BUILD)/scan_x64.o $(BUILD)/iso_x64.o $(BUILD)/config_x64.o $(BUILD)/gop_x64.o $(BUILD)/linux_x64.o $(BUILD)/sha_x64.o $(BUILD)/bootlog_x64.o

$(BUILD)/BOOTAA64.EFI: $(BUILD)/kam_aa64.o $(BUILD)/scan_aa64.o $(BUILD)/iso_aa64.o $(BUILD)/config_aa64.o $(BUILD)/gop_aa64.o $(BUILD)/linux_aa64.o $(BUILD)/sha_aa64.o $(BUILD)/bootlog_aa64.o linker/uefi_aa64.ld
	@test -x $(LD_LLD) || (echo "ld.lld missing: brew install lld"; exit 1)
	$(LD_LLD) -flavor link -subsystem:efi_application -entry:efi_main \
	  -out:$@ $(BUILD)/kam_aa64.o $(BUILD)/scan_aa64.o $(BUILD)/iso_aa64.o $(BUILD)/config_aa64.o $(BUILD)/gop_aa64.o $(BUILD)/linux_aa64.o $(BUILD)/sha_aa64.o $(BUILD)/bootlog_aa64.o

# --- HELLO.EFI chainload fixtures (entry: hello_main)
$(BUILD)/hello_x64.o: src/uefi/hello.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_X64) $(CFLAGS) src/uefi/hello.c -o $@

$(BUILD)/hello_aa64.o: src/uefi/hello.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_AA64) $(CFLAGS) src/uefi/hello.c -o $@

hello-x64: $(BUILD)/HELLOX64.EFI
hello-aa64: $(BUILD)/HELLOAA64.EFI

$(BUILD)/HELLOX64.EFI: $(BUILD)/hello_x64.o
	@test -x $(LD_LLD) || (echo "ld.lld missing: brew install lld"; exit 1)
	$(LD_LLD) -flavor link -subsystem:efi_application -entry:hello_main \
	  -out:$@ $(BUILD)/hello_x64.o

$(BUILD)/HELLOAA64.EFI: $(BUILD)/hello_aa64.o
	@test -x $(LD_LLD) || (echo "ld.lld missing: brew install lld"; exit 1)
	$(LD_LLD) -flavor link -subsystem:efi_application -entry:hello_main \
	  -out:$@ $(BUILD)/hello_aa64.o

esp: uefi-x64 kernel-x64 hello-x64 testiso
	python3 tools/mkesp.py --out $(BUILD)/esp.img --force --x64 $(BUILD)/BOOTX64.EFI --startup-nsh \
	  --extra $(BUILD)/kernel-x64.elf:KAM/KERNEL.ELF \
	  --extra $(BUILD)/HELLOX64.EFI:KAM/HELLO.EFI \
	  --extra $(BUILD)/test.iso:KAM/TEST.ISO

testiso: $(BUILD)/test.iso

$(BUILD)/test.iso: tools/mkiso.py
	@mkdir -p $(BUILD)
	python3 tools/mkiso.py --out $@

run-bios: bios-img
	qemu-system-x86_64 -drive format=raw,file=$(BUILD)/bios.img -nographic -net none

run-x64: esp
	qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=$(QEMU_X64_FW) \
	  -drive format=raw,file=$(BUILD)/esp.img -nographic -net none

run-aa64: uefi-aa64
	python3 tools/mkesp.py --out $(BUILD)/esp_aa64.img --force --aa64 $(BUILD)/BOOTAA64.EFI --startup-nsh
	qemu-system-aarch64 -M virt -cpu cortex-a72 -bios $(QEMU_AA64_FW) \
	  -drive format=raw,file=$(BUILD)/esp_aa64.img -nographic -net none -device ramfb

test-x64: esp
	python3 tools/drive_boot.py $(BUILD)/test_x64.log 40 'KAM-KERNEL' '' 20 -- \
	  qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=$(QEMU_X64_FW) \
	  -drive format=raw,file=$(BUILD)/esp.img -nographic -net none
	python3 tools/fatread.py $(BUILD)/esp.img KAM/BOOT.LOG | grep -q "exiting boot services"

test-chain-x64: esp
	python3 tools/drive_boot.py $(BUILD)/test_chain_x64.log 40 'KAM-HELLO' '2' 20 -- \
	  qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=$(QEMU_X64_FW) \
	  -drive format=raw,file=$(BUILD)/esp.img -nographic -net none

test-aa64: uefi-aa64 kernel-aa64 hello-aa64 testiso
	python3 tools/mkesp.py --out $(BUILD)/esp_aa64.img --force --aa64 $(BUILD)/BOOTAA64.EFI --startup-nsh \
	  --extra $(BUILD)/kernel-aa64.elf:KAM/KERNEL.ELF \
	  --extra $(BUILD)/HELLOAA64.EFI:KAM/HELLO.EFI \
	  --extra $(BUILD)/test.iso:KAM/TEST.ISO
	python3 tools/drive_boot.py $(BUILD)/test_aa64.log 90 'KAM-KERNEL' '' 55 -- \
	  qemu-system-aarch64 -M virt -cpu cortex-a72 -bios $(QEMU_AA64_FW) \
	  -drive format=raw,file=$(BUILD)/esp_aa64.img -nographic -net none -device ramfb
	python3 tools/fatread.py $(BUILD)/esp_aa64.img KAM/BOOT.LOG | grep -q "exiting boot services"

test-iso-x64: esp
	python3 tools/drive_boot.py $(BUILD)/test_iso_x64.log 40 'KAM-ISO-OK' '3' 20 -- \
	  qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=$(QEMU_X64_FW) \
	  -drive format=raw,file=$(BUILD)/esp.img -nographic -net none

test-iso-aa64: uefi-aa64 kernel-aa64 hello-aa64 testiso
	python3 tools/mkesp.py --out $(BUILD)/esp_aa64.img --force --aa64 $(BUILD)/BOOTAA64.EFI --startup-nsh \
	  --extra $(BUILD)/kernel-aa64.elf:KAM/KERNEL.ELF \
	  --extra $(BUILD)/HELLOAA64.EFI:KAM/HELLO.EFI \
	  --extra $(BUILD)/test.iso:KAM/TEST.ISO
	python3 tools/drive_boot.py $(BUILD)/test_iso_aa64.log 90 'KAM-ISO-OK' '3' 55 -- \
	  qemu-system-aarch64 -M virt -cpu cortex-a72 -bios $(QEMU_AA64_FW) \
	  -drive format=raw,file=$(BUILD)/esp_aa64.img -nographic -net none -device ramfb

# --- Static config test images (timeout 1, default 2 -> HELLO, no keys)
$(BUILD)/testcfg.ini:
	@mkdir -p $(BUILD)
	printf '%s\n' '# KAM test config' 'timeout 1' 'default 2' '' \
	  '[kernel]' 'label Test Kernel Entry' 'path \KAM\KERNEL.ELF' '' \
	  '[chain]' 'label Hello Chain Entry' 'path \KAM\HELLO.EFI' > $@

$(BUILD)/esp_cfg.img: uefi-x64 kernel-x64 hello-x64 testiso $(BUILD)/testcfg.ini
	python3 tools/mkesp.py --out $@ --force --x64 $(BUILD)/BOOTX64.EFI --startup-nsh \
	  --extra $(BUILD)/kernel-x64.elf:KAM/KERNEL.ELF \
	  --extra $(BUILD)/HELLOX64.EFI:KAM/HELLO.EFI \
	  --extra $(BUILD)/test.iso:KAM/TEST.ISO \
	  --extra $(BUILD)/testcfg.ini:KAM/KAM.INI

$(BUILD)/esp_cfg_aa64.img: uefi-aa64 kernel-aa64 hello-aa64 testiso $(BUILD)/testcfg.ini
	python3 tools/mkesp.py --out $@ --force --aa64 $(BUILD)/BOOTAA64.EFI --startup-nsh \
	  --extra $(BUILD)/kernel-aa64.elf:KAM/KERNEL.ELF \
	  --extra $(BUILD)/HELLOAA64.EFI:KAM/HELLO.EFI \
	  --extra $(BUILD)/test.iso:KAM/TEST.ISO \
	  --extra $(BUILD)/testcfg.ini:KAM/KAM.INI

test-config-x64: $(BUILD)/esp_cfg.img
	python3 tools/drive_boot.py $(BUILD)/test_config_x64.log 40 'KAM-HELLO' '' 20 -- \
	  qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=$(QEMU_X64_FW) \
	  -drive format=raw,file=$(BUILD)/esp_cfg.img -nographic -net none

test-config-aa64: $(BUILD)/esp_cfg_aa64.img
	python3 tools/drive_boot.py $(BUILD)/test_config_aa64.log 90 'KAM-HELLO' '' 55 -- \
	  qemu-system-aarch64 -M virt -cpu cortex-a72 -bios $(QEMU_AA64_FW) \
	  -drive format=raw,file=$(BUILD)/esp_cfg_aa64.img -nographic -net none -device ramfb

# --- Windows stub layout (bootmgfw = HELLO copy, BCD/WIM marker stubs)
$(BUILD)/WINMGFW.EFI: $(BUILD)/HELLOX64.EFI
	cp $(BUILD)/HELLOX64.EFI $@

$(BUILD)/win_bcd.stub:
	printf 'KAM-WIN-STUB-BCD' > $@

$(BUILD)/win_wim.stub:
	printf 'KAM-WIN-STUB-WIM' > $@

$(BUILD)/esp_win.img: uefi-x64 kernel-x64 hello-x64 testiso $(BUILD)/WINMGFW.EFI $(BUILD)/win_bcd.stub $(BUILD)/win_wim.stub
	python3 tools/mkesp.py --out $@ --force --x64 $(BUILD)/BOOTX64.EFI --startup-nsh \
	  --extra $(BUILD)/kernel-x64.elf:KAM/KERNEL.ELF \
	  --extra $(BUILD)/HELLOX64.EFI:KAM/HELLO.EFI \
	  --extra $(BUILD)/test.iso:KAM/TEST.ISO \
	  --extra $(BUILD)/WINMGFW.EFI:EFI/Microsoft/Boot/bootmgfw.efi \
	  --extra $(BUILD)/win_bcd.stub:EFI/Microsoft/Boot/BCD \
	  --extra $(BUILD)/win_wim.stub:sources/install.wim

test-win-x64: $(BUILD)/esp_win.img
	python3 tools/drive_boot.py $(BUILD)/test_win_x64.log 40 'KAM-HELLO' '2' 20 -- \
	  qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=$(QEMU_X64_FW) \
	  -drive format=raw,file=$(BUILD)/esp_win.img -nographic -net none

# --- Second test volume (HELLO2 at root, nothing else)
$(BUILD)/hello2_x64.o: src/uefi/hello2.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_X64) $(CFLAGS) src/uefi/hello2.c -o $@

$(BUILD)/HELLO2X64.EFI: $(BUILD)/hello2_x64.o
	@test -x $(LD_LLD) || (echo "ld.lld missing: brew install lld"; exit 1)
	$(LD_LLD) -flavor link -subsystem:efi_application -entry:hello2_main \
	  -out:$@ $(BUILD)/hello2_x64.o

$(BUILD)/hello3_x64.o: src/uefi/hello3.c $(KAM_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_X64) $(CFLAGS) src/uefi/hello3.c -o $@

$(BUILD)/HELLO3X64.EFI: $(BUILD)/hello3_x64.o
	@test -x $(LD_LLD) || (echo "ld.lld missing: brew install lld"; exit 1)
	$(LD_LLD) -flavor link -subsystem:efi_application -entry:hello3_main \
	  -out:$@ $(BUILD)/hello3_x64.o

$(BUILD)/esp2.img: $(BUILD)/HELLO2X64.EFI
	python3 tools/mkesp.py --out $@ --force --sectors 16384 \
	  --extra $(BUILD)/HELLO2X64.EFI:HELLO2.EFI

test-multivol-x64: esp $(BUILD)/esp2.img
	python3 tools/drive_boot.py $(BUILD)/test_multivol_x64.log 70 'KAM-HELLO2' '4' 40 -- \
	  qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=$(QEMU_X64_FW) \
	  -drive format=raw,file=$(BUILD)/esp.img \
	  -drive format=raw,file=$(BUILD)/esp2.img -nographic -net none

# --- Two-partition image (ESP + data) proving same-disk second volumes
$(BUILD)/esp_parts.img: uefi-x64 kernel-x64 hello-x64 testiso $(BUILD)/HELLO3X64.EFI
	python3 tools/mkesp.py --out $@ --force --x64 $(BUILD)/BOOTX64.EFI --startup-nsh \
	  --extra $(BUILD)/kernel-x64.elf:KAM/KERNEL.ELF \
	  --extra $(BUILD)/HELLOX64.EFI:KAM/HELLO.EFI \
	  --extra $(BUILD)/test.iso:KAM/TEST.ISO \
	  --data-mb 8 --data-extra $(BUILD)/HELLO3X64.EFI:HELLO3.EFI

test-parts-x64: $(BUILD)/esp_parts.img
	python3 tools/drive_boot.py $(BUILD)/test_parts_x64.log 70 'KAM-HELLO3' '4' 40 -- \
	  qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=$(QEMU_X64_FW) \
	  -drive format=raw,file=$(BUILD)/esp_parts.img -nographic -net none

# --- Host unit tests (pure logic + image structure, no firmware)
$(BUILD)/unittest: tests/unittest.c src/uefi/iso.c src/uefi/config.c src/uefi/sha256.c include/kam/*.h
	cc -I include tests/unittest.c src/uefi/iso.c src/uefi/config.c src/uefi/sha256.c -o $@ -Wall -Wextra

unittest: testiso $(BUILD)/unittest
	./$(BUILD)/unittest
	python3 tools/mkesp.py --out $(BUILD)/ut.img --force --sectors 8192 --x64 /dev/null \
	  --extra $(BUILD)/test.iso:KAM/TEST.ISO \
	  --extra $(BUILD)/test.iso:KAM/VERYLONGNAME.TXT
	python3 tools/imgcheck.py $(BUILD)/ut.img --expect KAM/TEST.ISO
	python3 tools/imgcheck.py $(BUILD)/esp.img --expect KAM/KERNEL.ELF
	python3 tools/isoinfo.py $(BUILD)/test.iso --sha256 | grep -q HELLO.TXT
	python3 tools/mkesp.py --out /dev/null --force --x64 /dev/null 2>&1 | grep -q refusing
	python3 tools/mkesp.py --out $(BUILD)/ut.img --sectors 8192 --x64 /dev/null 2>&1 | grep -q "use --force"
	@echo "unittest: images OK"

# --- Password test images (hash of "kamboot", fixed test salt)
$(BUILD)/testpwd.ini:
	@mkdir -p $(BUILD)
	python3 tools/mkpasswd.py --password kamboot --salt KAMTESTSALT --timeout 5 > $@
	printf '%s\n' '' '[kernel]' 'label Pwd Kernel' 'path \KAM\KERNEL.ELF' >> $@

$(BUILD)/esp_pwd.img: uefi-x64 kernel-x64 $(BUILD)/testpwd.ini
	python3 tools/mkesp.py --out $@ --force --x64 $(BUILD)/BOOTX64.EFI --startup-nsh \
	  --extra $(BUILD)/kernel-x64.elf:KAM/KERNEL.ELF \
	  --extra $(BUILD)/testpwd.ini:KAM/KAM.INI

$(BUILD)/esp_pwd_aa64.img: uefi-aa64 kernel-aa64 $(BUILD)/testpwd.ini
	python3 tools/mkesp.py --out $@ --force --aa64 $(BUILD)/BOOTAA64.EFI --startup-nsh \
	  --extra $(BUILD)/kernel-aa64.elf:KAM/KERNEL.ELF \
	  --extra $(BUILD)/testpwd.ini:KAM/KAM.INI

test-pwd-x64: $(BUILD)/esp_pwd.img
	python3 tools/drive_boot.py $(BUILD)/test_pwd_x64.log 40 'KAM-KERNEL' 'kamboot\r' 20 -- \
	  qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=$(QEMU_X64_FW) \
	  -drive format=raw,file=$(BUILD)/esp_pwd.img -nographic -net none

test-pwddeny-x64: $(BUILD)/esp_pwd.img
	python3 tools/drive_boot.py $(BUILD)/test_pwddeny_x64.log 60 'access denied' 'wrong\r' 40 -- \
	  qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=$(QEMU_X64_FW) \
	  -drive format=raw,file=$(BUILD)/esp_pwd.img -nographic -net none

test-pwd-aa64: $(BUILD)/esp_pwd_aa64.img
	python3 tools/drive_boot.py $(BUILD)/test_pwd_aa64.log 90 'KAM-KERNEL' 'kamboot\r' 55 -- \
	  qemu-system-aarch64 -M virt -cpu cortex-a72 -bios $(QEMU_AA64_FW) \
	  -drive format=raw,file=$(BUILD)/esp_pwd_aa64.img -nographic -net none -device ramfb

test-pwddeny-aa64: $(BUILD)/esp_pwd_aa64.img
	python3 tools/drive_boot.py $(BUILD)/test_pwddeny_aa64.log 120 'access denied' 'wrong\r' 80 -- \
	  qemu-system-aarch64 -M virt -cpu cortex-a72 -bios $(QEMU_AA64_FW) \
	  -drive format=raw,file=$(BUILD)/esp_pwd_aa64.img -nographic -net none -device ramfb

# --- Linux fixture (flat bzImage speaking the boot protocol)
$(BUILD)/vmlinuz_x64.o: src/linux/vmlinuz.c $(KERN_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_X64_LNX) $(KCFLAGS) src/linux/vmlinuz.c -o $@

$(BUILD)/vmlinuz_aa64.o: src/linux/vmlinuz.c $(KERN_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_AA64_LNX) $(KCFLAGS) src/linux/vmlinuz.c -o $@

$(BUILD)/vmlinuz-x64.elf: $(BUILD)/vmlinuz_x64.o linker/vmlinuz_x64.ld
	$(LD_LLD) -T linker/vmlinuz_x64.ld -o $@ $(BUILD)/vmlinuz_x64.o

$(BUILD)/vmlinuz-aa64.elf: $(BUILD)/vmlinuz_aa64.o linker/vmlinuz_aa64.ld
	$(LD_LLD) -T linker/vmlinuz_aa64.ld -o $@ $(BUILD)/vmlinuz_aa64.o

$(BUILD)/vmlinuz-x64.bin: $(BUILD)/vmlinuz-x64.elf
	$(LLVM_OBJCOPY) -O binary $< $@
	@sz=$$(wc -c < $@); test "$$sz" -le 65536 || (echo "vmlinuz too big: $$sz"; exit 1)

$(BUILD)/vmlinuz-aa64.bin: $(BUILD)/vmlinuz-aa64.elf
	$(LLVM_OBJCOPY) -O binary $< $@
	@sz=$$(wc -c < $@); test "$$sz" -le 65536 || (echo "vmlinuz too big: $$sz"; exit 1)

$(BUILD)/test_initrd.img:
	printf 'KAM-INITRD-DATA' > $@

$(BUILD)/testcfg-linux.ini:
	@mkdir -p $(BUILD)
	printf '%s\n' '# KAM linux test config' 'timeout 1' 'default 1' '' \
	  '[linux]' 'label Test Linux Entry' 'path \KAM\VMLINUZ' \
	  'initrd \KAM\INITRD.IMG' 'cmdline kam-test console=ttyS0' > $@

$(BUILD)/esp_linux.img: uefi-x64 $(BUILD)/vmlinuz-x64.bin $(BUILD)/test_initrd.img $(BUILD)/testcfg-linux.ini
	python3 tools/mkesp.py --out $@ --force --x64 $(BUILD)/BOOTX64.EFI --startup-nsh \
	  --extra $(BUILD)/vmlinuz-x64.bin:KAM/VMLINUZ \
	  --extra $(BUILD)/test_initrd.img:KAM/INITRD.IMG \
	  --extra $(BUILD)/testcfg-linux.ini:KAM/KAM.INI

$(BUILD)/esp_linux_aa64.img: uefi-aa64 $(BUILD)/vmlinuz-aa64.bin $(BUILD)/test_initrd.img $(BUILD)/testcfg-linux.ini
	python3 tools/mkesp.py --out $@ --force --aa64 $(BUILD)/BOOTAA64.EFI --startup-nsh \
	  --extra $(BUILD)/vmlinuz-aa64.bin:KAM/VMLINUZ \
	  --extra $(BUILD)/test_initrd.img:KAM/INITRD.IMG \
	  --extra $(BUILD)/testcfg-linux.ini:KAM/KAM.INI

test-linux-x64: $(BUILD)/esp_linux.img
	python3 tools/drive_boot.py $(BUILD)/test_linux_x64.log 40 'KAM-BZIMAGE' '' 20 -- \
	  qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=$(QEMU_X64_FW) \
	  -drive format=raw,file=$(BUILD)/esp_linux.img -nographic -net none

test-linux-aa64: $(BUILD)/esp_linux_aa64.img
	python3 tools/drive_boot.py $(BUILD)/test_linux_aa64.log 90 'KAM-BZIMAGE' '' 55 -- \
	  qemu-system-aarch64 -M virt -cpu cortex-a72 -bios $(QEMU_AA64_FW) \
	  -drive format=raw,file=$(BUILD)/esp_linux_aa64.img -nographic -net none -device ramfb

# --- Bootable ISO (El Torito BIOS entry + UEFI FAT entry)
$(BUILD)/mbr_cd.bin: src/bios/mbr.asm
	@mkdir -p $(BUILD)
	nasm -f bin -DCDROM=1 $< -o $@

$(BUILD)/esp_cd.img: uefi-x64 kernel-x64
	python3 tools/mkesp.py --out $@ --force --sectors 16384 --superfloppy --x64 $(BUILD)/BOOTX64.EFI \
	  --extra $(BUILD)/kernel-x64.elf:KAM/KERNEL.ELF

$(BUILD)/cd.iso: testiso bios $(BUILD)/stage2.bin $(BUILD)/kernel-x64.elf $(BUILD)/mbr_cd.bin $(BUILD)/esp_cd.img
	python3 tools/mkiso.py --bootable --out $@ \
	  --stage2 $(BUILD)/stage2.bin --kernel $(BUILD)/kernel-x64.elf \
	  --mbr $(BUILD)/mbr_cd.bin --efiimg $(BUILD)/esp_cd.img

test-cd-bios: $(BUILD)/cd.iso
	timeout 30 qemu-system-x86_64 -cdrom $(BUILD)/cd.iso -boot d \
	  -nographic -net none > $(BUILD)/test_cd_bios.log 2>&1; \
	grep -a -q "KAM-KERNEL" $(BUILD)/test_cd_bios.log && \
	echo "PASS: BIOS El Torito boot reached the kernel" || \
	(echo "FAIL: no KAM-KERNEL from CD"; tr -d '\0' < $(BUILD)/test_cd_bios.log | tail -n 8; exit 1)

test-cd-efi: $(BUILD)/cd.iso
	python3 tools/drive_boot.py $(BUILD)/test_cd_efi.log 60 'KAM-KERNEL' '' 30 -- \
	  qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=$(QEMU_X64_FW) \
	  -drive file=$(BUILD)/cd.iso,media=cdrom -nographic -net none

# --- Bootable USB stick (MBR table + gap payload + ESP partition)
$(BUILD)/usb.img: bios-img esp
	python3 tools/mkusb.py --mbr $(BUILD)/mbr.bin \
	  --stage2 $(BUILD)/stage2.bin --kernel $(BUILD)/kernel-x64.elf \
	  --esp $(BUILD)/esp.img --out $@

test-usb-bios: $(BUILD)/usb.img
	timeout 30 qemu-system-x86_64 -drive file=$(BUILD)/usb.img,format=raw,if=none,id=U \
	  -device qemu-xhci -device usb-storage,drive=U -boot order=d \
	  -nographic -net none > $(BUILD)/test_usb_bios.log 2>&1; \
	grep -a -q "KAM-KERNEL" $(BUILD)/test_usb_bios.log && \
	echo "PASS: BIOS USB boot reached the kernel" || \
	(echo "FAIL: no KAM-KERNEL from USB"; tr -d '\0' < $(BUILD)/test_usb_bios.log | tail -n 8; exit 1)

test-usb-efi: $(BUILD)/usb.img
	python3 tools/drive_boot.py $(BUILD)/test_usb_efi.log 60 'KAM-KERNEL' '' 30 -- \
	  qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=$(QEMU_X64_FW) \
	  -drive file=$(BUILD)/usb.img,format=raw,if=none,id=U \
	  -device qemu-xhci -device usb-storage,drive=U -nographic -net none

test-gop-x64: esp
	python3 tools/shot_boot.py $(BUILD)/test_gop_x64.log 12 40 -- \
	  qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=$(QEMU_X64_FW) \
	  -drive format=raw,file=$(BUILD)/esp.img -nographic -net none

test-gop-aa64: uefi-aa64 kernel-aa64 hello-aa64 testiso
	python3 tools/mkesp.py --out $(BUILD)/esp_aa64.img --force --aa64 $(BUILD)/BOOTAA64.EFI --startup-nsh \
	  --extra $(BUILD)/kernel-aa64.elf:KAM/KERNEL.ELF \
	  --extra $(BUILD)/HELLOAA64.EFI:KAM/HELLO.EFI \
	  --extra $(BUILD)/test.iso:KAM/TEST.ISO
	python3 tools/shot_boot.py $(BUILD)/test_gop_aa64.log 45 90 -- \
	  qemu-system-aarch64 -M virt -cpu cortex-a72 -bios $(QEMU_AA64_FW) \
	  -drive format=raw,file=$(BUILD)/esp_aa64.img -nographic -net none -device ramfb

test-chain-aa64: uefi-aa64 kernel-aa64 hello-aa64
	python3 tools/mkesp.py --out $(BUILD)/esp_aa64.img --force --aa64 $(BUILD)/BOOTAA64.EFI --startup-nsh \
	  --extra $(BUILD)/kernel-aa64.elf:KAM/KERNEL.ELF \
	  --extra $(BUILD)/HELLOAA64.EFI:KAM/HELLO.EFI
	python3 tools/drive_boot.py $(BUILD)/test_chain_aa64.log 90 'KAM-HELLO' '2' 55 -- \
	  qemu-system-aarch64 -M virt -cpu cortex-a72 -bios $(QEMU_AA64_FW) \
	  -drive format=raw,file=$(BUILD)/esp_aa64.img -nographic -net none -device ramfb

# --- Test kernel (ELF64, linked at 1MB, loaded by both paths)
$(BUILD)/kam_kernel_x64.o: src/kernel/kam_kernel.c $(KERN_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_X64_LNX) $(KCFLAGS) src/kernel/kam_kernel.c -o $@

$(BUILD)/kam_kernel_aa64.o: src/kernel/kam_kernel.c $(KERN_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_AA64_LNX) $(KCFLAGS) src/kernel/kam_kernel.c -o $@

kernel-x64: $(BUILD)/kernel-x64.elf
kernel-aa64: $(BUILD)/kernel-aa64.elf

$(BUILD)/kernel-x64.elf: $(BUILD)/kam_kernel_x64.o linker/kernel_x64.ld
	$(LD_LLD) -T linker/kernel_x64.ld -o $@ $(BUILD)/kam_kernel_x64.o

$(BUILD)/kernel-aa64.elf: $(BUILD)/kam_kernel_aa64.o linker/kernel_aa64.ld
	$(LD_LLD) -T linker/kernel_aa64.ld -o $@ $(BUILD)/kam_kernel_aa64.o

# --- BIOS stage2 chain: trampoline (nasm, fixed 2KB) + C payload (fixed 0x8800)
$(BUILD)/kam_bios.o: src/bios/stage2_main.c $(BIOS_HEADERS)
	@mkdir -p $(BUILD)
	$(CC_BIOS) $(BCFLAGS) src/bios/stage2_main.c -o $@

$(BUILD)/payload.elf: $(BUILD)/kam_bios.o linker/bios_payload.ld
	$(LD_LLD) -T linker/bios_payload.ld -o $@ $(BUILD)/kam_bios.o

$(BUILD)/payload.bin: $(BUILD)/payload.elf
	$(LLVM_OBJCOPY) -O binary $< $@
	@sz=$$(wc -c < $@); test "$$sz" -le 6144 || (echo "payload too big: $$sz"; exit 1)

$(BUILD)/stage2_tramp.bin: src/bios/stage2.asm
	@mkdir -p $(BUILD)
	nasm -f bin $< -o $@
	@sz=$$(wc -c < $@); test "$$sz" -eq 2048 || (echo "trampoline must be 2048: $$sz"; exit 1)

$(BUILD)/stage2.bin: $(BUILD)/stage2_tramp.bin $(BUILD)/payload.bin
	cat $(BUILD)/stage2_tramp.bin $(BUILD)/payload.bin > $@

bios-img: bios $(BUILD)/stage2.bin $(BUILD)/kernel-x64.elf
	@mkdir -p $(BUILD)
	@sz=$$(wc -c < $(BUILD)/kernel-x64.elf); test "$$sz" -le 65536 || (echo "kernel too big: $$sz"; exit 1)
	cp $(BUILD)/mbr.bin $(BUILD)/bios.img
	dd if=$(BUILD)/stage2.bin of=$(BUILD)/bios.img bs=512 seek=1 conv=notrunc status=none
	dd if=$(BUILD)/kernel-x64.elf of=$(BUILD)/bios.img bs=512 seek=17 conv=notrunc status=none
	truncate -s 81920 $(BUILD)/bios.img
	@echo "BIOS image: $(BUILD)/bios.img ($$(wc -c < $(BUILD)/bios.img) bytes)"

test-bios: bios-img
	timeout 20 qemu-system-x86_64 -drive format=raw,file=$(BUILD)/bios.img \
	  -nographic -net none > $(BUILD)/test_bios.log 2>&1; \
	grep -a -q "KAM-KERNEL" $(BUILD)/test_bios.log && \
	echo "PASS: BIOS path reached the kernel" || \
	(echo "FAIL: no KAM-KERNEL output"; tr -d '\0' < $(BUILD)/test_bios.log | tail -n 12; exit 1)

clean:
	rm -rf $(BUILD)

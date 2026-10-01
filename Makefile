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
                include/kam/bios_addrs.h include/kam/bios_console.h

.PHONY: all bios bios-img check-uefi uefi-x64 uefi-aa64 esp run-x64 run-aa64 run-bios test-x64 test-aa64 test-bios clean

all: bios-img check-uefi uefi-x64 uefi-aa64

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

$(BUILD)/BOOTX64.EFI: $(BUILD)/kam_x64.o linker/uefi_x64.ld
	@test -x $(LD_LLD) || (echo "ld.lld missing: brew install lld"; exit 1)
	$(LD_LLD) -flavor link -subsystem:efi_application -entry:efi_main \
	  -out:$@ $(BUILD)/kam_x64.o

$(BUILD)/BOOTAA64.EFI: $(BUILD)/kam_aa64.o linker/uefi_aa64.ld
	@test -x $(LD_LLD) || (echo "ld.lld missing: brew install lld"; exit 1)
	$(LD_LLD) -flavor link -subsystem:efi_application -entry:efi_main \
	  -out:$@ $(BUILD)/kam_aa64.o

esp: uefi-x64
	python3 tools/mkesp.py --out $(BUILD)/esp.img --x64 $(BUILD)/BOOTX64.EFI --startup-nsh

run-bios: bios-img
	qemu-system-x86_64 -drive format=raw,file=$(BUILD)/bios.img -nographic -net none

run-x64: esp
	qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=$(QEMU_X64_FW) \
	  -drive format=raw,file=$(BUILD)/esp.img -nographic -net none

run-aa64: uefi-aa64
	python3 tools/mkesp.py --out $(BUILD)/esp_aa64.img --aa64 $(BUILD)/BOOTAA64.EFI --startup-nsh
	qemu-system-aarch64 -M virt -cpu cortex-a72 -bios $(QEMU_AA64_FW) \
	  -drive format=raw,file=$(BUILD)/esp_aa64.img -nographic -net none -device ramfb

test-x64: esp
	sh tools/boot_test.sh $(BUILD)/test_x64.log '\EFI\BOOT\BOOTX64.EFI' 45 -- \
	  qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=$(QEMU_X64_FW) \
	  -drive format=raw,file=$(BUILD)/esp.img -nographic -net none

test-aa64: uefi-aa64
	python3 tools/mkesp.py --out $(BUILD)/esp_aa64.img --aa64 $(BUILD)/BOOTAA64.EFI --startup-nsh
	sh tools/boot_test.sh $(BUILD)/test_aa64.log '\EFI\BOOT\BOOTAA64.EFI' 60 -- \
	  qemu-system-aarch64 -M virt -cpu cortex-a72 -bios $(QEMU_AA64_FW) \
	  -drive format=raw,file=$(BUILD)/esp_aa64.img -nographic -net none -device ramfb

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

bios-img: bios $(BUILD)/stage2.bin
	@mkdir -p $(BUILD)
	cp $(BUILD)/mbr.bin $(BUILD)/bios.img
	dd if=$(BUILD)/stage2.bin of=$(BUILD)/bios.img bs=512 seek=1 conv=notrunc status=none
	truncate -s 16384 $(BUILD)/bios.img
	@echo "BIOS image: $(BUILD)/bios.img ($$(wc -c < $(BUILD)/bios.img) bytes)"

test-bios: bios-img
	timeout 20 qemu-system-x86_64 -drive format=raw,file=$(BUILD)/bios.img \
	  -nographic -net none > $(BUILD)/test_bios.log 2>&1; \
	grep -a -q "KAM BIOS stage2" $(BUILD)/test_bios.log && \
	grep -a -q "Free RAM" $(BUILD)/test_bios.log && \
	echo "PASS: BIOS stage2 booted in QEMU" || \
	(echo "FAIL: no BIOS stage2 output"; tr -d '\0' < $(BUILD)/test_bios.log | tail -n 12; exit 1)

clean:
	rm -rf $(BUILD)

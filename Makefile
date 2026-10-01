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
               include/kam/scan.h
KERN_HEADERS := include/kam/types.h include/kam/memmap.h \
                include/kam/elf.h include/kam/raw_serial.h

CC_X64_LNX  := $(LLVM)/clang --target=x86_64-unknown-linux-gnu
CC_AA64_LNX := $(LLVM)/clang --target=aarch64-unknown-linux-gnu
KCFLAGS  := -ffreestanding -nostdlib -fno-stack-protector \
            -fno-unwind-tables -fno-asynchronous-unwind-tables \
            -mno-red-zone -Wall -Wextra -O2 -Iinclude -c

.PHONY: all bios bios-img check-uefi uefi-x64 uefi-aa64 kernel-x64 kernel-aa64 hello-x64 hello-aa64 esp run-x64 run-aa64 run-bios test-x64 test-aa64 test-chain-x64 test-chain-aa64 test-bios clean

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

$(BUILD)/BOOTX64.EFI: $(BUILD)/kam_x64.o $(BUILD)/scan_x64.o linker/uefi_x64.ld
	@test -x $(LD_LLD) || (echo "ld.lld missing: brew install lld"; exit 1)
	$(LD_LLD) -flavor link -subsystem:efi_application -entry:efi_main \
	  -out:$@ $(BUILD)/kam_x64.o $(BUILD)/scan_x64.o

$(BUILD)/BOOTAA64.EFI: $(BUILD)/kam_aa64.o $(BUILD)/scan_aa64.o linker/uefi_aa64.ld
	@test -x $(LD_LLD) || (echo "ld.lld missing: brew install lld"; exit 1)
	$(LD_LLD) -flavor link -subsystem:efi_application -entry:efi_main \
	  -out:$@ $(BUILD)/kam_aa64.o $(BUILD)/scan_aa64.o

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

esp: uefi-x64 kernel-x64 hello-x64
	python3 tools/mkesp.py --out $(BUILD)/esp.img --x64 $(BUILD)/BOOTX64.EFI --startup-nsh \
	  --extra $(BUILD)/kernel-x64.elf:KAM/KERNEL.ELF \
	  --extra $(BUILD)/HELLOX64.EFI:KAM/HELLO.EFI

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
	python3 tools/drive_boot.py $(BUILD)/test_x64.log 40 'KAM-KERNEL' '' 20 -- \
	  qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=$(QEMU_X64_FW) \
	  -drive format=raw,file=$(BUILD)/esp.img -nographic -net none

test-chain-x64: esp
	python3 tools/drive_boot.py $(BUILD)/test_chain_x64.log 40 'KAM-HELLO' '2' 20 -- \
	  qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,file=$(QEMU_X64_FW) \
	  -drive format=raw,file=$(BUILD)/esp.img -nographic -net none

test-aa64: uefi-aa64 kernel-aa64 hello-aa64
	python3 tools/mkesp.py --out $(BUILD)/esp_aa64.img --aa64 $(BUILD)/BOOTAA64.EFI --startup-nsh \
	  --extra $(BUILD)/kernel-aa64.elf:KAM/KERNEL.ELF \
	  --extra $(BUILD)/HELLOAA64.EFI:KAM/HELLO.EFI
	python3 tools/drive_boot.py $(BUILD)/test_aa64.log 90 'KAM-KERNEL' '' 55 -- \
	  qemu-system-aarch64 -M virt -cpu cortex-a72 -bios $(QEMU_AA64_FW) \
	  -drive format=raw,file=$(BUILD)/esp_aa64.img -nographic -net none -device ramfb

test-chain-aa64: uefi-aa64 kernel-aa64 hello-aa64
	python3 tools/mkesp.py --out $(BUILD)/esp_aa64.img --aa64 $(BUILD)/BOOTAA64.EFI --startup-nsh \
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

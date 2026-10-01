; KAM MBR boot sector. x86 16-bit real mode, 512 bytes.
; prints "KAM" via BIOS int 0x10, signed with 0xAA55.
; The stage2 load step comes next (idles here for now).

BITS 16
CPU 386
ORG 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld

    mov si, msg
.print:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .print
.done:
    ; No disk signature / partition table (empty MBR).
    ; LBA read via int 0x13 will be added in the stage2 step.
.halt:
    hlt
    jmp .halt

msg: db 'KAM BIOS MBR', 13, 10, 0

TIMES 510 - ($ - $$) db 0
DW 0xAA55

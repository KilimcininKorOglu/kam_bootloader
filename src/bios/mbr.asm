; KAM MBR boot sector. x86 16-bit real mode, 512 bytes.
; prints "KAM", loads 16 sectors (stage2) via LBA
; (int 0x13 AH=0x42) to 0x8000, then far-jumps there.
; Boot drive is passed in DL (saved first, BIOS calls may clobber it).

BITS 16
CPU 386
ORG 0x7C00

%define STAGE2_LOAD  0x8000
%define STAGE2_SECT  16
%define DRIVE_BOX    0x0500
%define DAP_ADDR     0x0600

start:
    mov [DRIVE_BOX], dl      ; save boot drive before anything clobbers it
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld

    mov si, msg
    call puts

    ; Build Disk Address Packet at 0x0600.
    mov dl, [DRIVE_BOX]
    mov si, DAP_ADDR
    mov byte [si + 0], 16    ; size
    mov byte [si + 1], 0     ; reserved
    mov word [si + 2], STAGE2_SECT
    mov word [si + 4], STAGE2_LOAD
    mov word [si + 6], 0
    mov dword [si + 8], 1    ; LBA 1
    mov dword [si + 12], 0
    mov ah, 0x42
    int 0x13
    jnc .loaded
    mov si, msg_dap
    call puts
.halt:
    hlt
    jmp .halt
.loaded:
    mov dl, [DRIVE_BOX]      ; restore boot drive for stage2
    jmp 0x0000:STAGE2_LOAD

; DS:SI = ASCIZ, BIOS teletype.
puts:
    push ax
    push bx
    push si
.next:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .next
.done:
    pop si
    pop bx
    pop ax
    ret

msg:     db 'KAM BIOS MBR', 13, 10, 0
msg_dap: db 'KAM MBR: DAP ERR', 13, 10, 0

TIMES 510 - ($ - $$) db 0
DW 0xAA55

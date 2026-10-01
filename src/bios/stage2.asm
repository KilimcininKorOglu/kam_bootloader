; KAM BIOS stage2 trampoline. Loaded by the MBR at 0x8000 (16 sectors).
; 16-bit real mode -> A20 -> E820 map -> protected mode -> paging ->
; 64-bit long mode -> call kam_bios_main (C payload at 0x8800).
; nasm -f bin, padded to exactly 2048 bytes (TIMES).
; Fixed addresses mirror include/kam/bios_addrs.h.

CPU X86-64
BITS 16
ORG 0x8000

%define E820_BASE  0x5000
%define E820_MAX   64
%define PML4_BASE  0x10000
%define STACK64    0x90000
%define PAYLOAD    0x8800

entry:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000
    sti
    cld

    call serial_init
    mov si, msg_stage2
    call puts16

    call a20_enable
    mov si, msg_a20
    call puts16

    call e820_probe
    mov si, msg_e820
    call puts16

    mov si, msg_long
    call puts16

    ; --- protected mode ---
    cli
    lgdt [gdt_ptr]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm32

; ---------------- 16-bit helpers ----------------

; AL = char. Trashes DX.
serial_putc16:
    push dx
    push ax
    mov dx, 0x3FD
.wait:
    in al, dx
    test al, 0x20
    jz .wait
    pop ax
    mov dx, 0x3F8
    out dx, al
    pop dx
    ret

serial_init:
    push dx
    push ax
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8
    mov al, 0x01
    out dx, al
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x03
    out dx, al
    pop ax
    pop dx
    ret

; DS:SI = ASCIZ. Expands \n to \r\n.
puts16:
    push ax
    push si
.next:
    lodsb
    test al, al
    jz .done
    cmp al, 10
    jne .out
    mov al, 13
    call serial_putc16
    mov al, 10
.out:
    call serial_putc16
    jmp .next
.done:
    pop si
    pop ax
    ret

kbd_wait_w:
    in al, 0x64
    test al, 2
    jnz kbd_wait_w
    ret

; Try BIOS, keyboard controller, then fast gate. Best effort.
a20_enable:
    push ax
    mov ax, 0x2401
    int 0x15
    call kbd_wait_w
    mov al, 0xD1
    out 0x64, al
    call kbd_wait_w
    mov al, 0xDF
    out 0x60, al
    call kbd_wait_w
    in al, 0x92
    or al, 2
    out 0x92, al
    pop ax
    ret

; Fill kam_memmap at 0x5000 via int 0x15 E820. Count -> [0x5000].
e820_probe:
    pushad
    push es
    xor eax, eax
    mov es, eax
    mov dword [es:E820_BASE], 0
    xor ebx, ebx
    mov di, E820_BASE + 8
.next:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    mov dword [es:di + 20], 1
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    cmp ecx, 20
    jb .cont
    add di, 24
    inc dword [es:E820_BASE]
    cmp dword [es:E820_BASE], E820_MAX
    jae .done
.cont:
    test ebx, ebx
    jnz .next
.done:
    pop es
    popad
    ret

; ---------------- 32-bit ----------------
BITS 32
pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7000

    ; Clear 3 paging pages at 0x10000.
    xor eax, eax
    mov edi, PML4_BASE
    mov ecx, 1024 * 3
    rep stosd

    ; PML4[0] -> PDPT, PDPT[0] -> PD.
    mov dword [PML4_BASE], PML4_BASE + 0x1000 + 3
    mov dword [PML4_BASE + 0x1000], PML4_BASE + 0x2000 + 3

    ; PD: 512 x 2MB identity pages (covers 0..1GB).
    mov edi, PML4_BASE + 0x2000
    mov eax, 0x83
    mov ecx, 512
.pd:
    mov [edi], eax
    mov dword [edi + 4], 0
    add eax, 0x200000
    add edi, 8
    loop .pd

    ; PAE + load PML4.
    mov eax, cr4
    or eax, (1 << 5) | (1 << 9) | (1 << 10)  ; PAE + OSFXSR + OSXMMEXCPT
    mov cr4, eax
    mov eax, PML4_BASE
    mov cr3, eax

    ; Floating point: clear EM, set MP (clang may emit SSE).
    mov eax, cr0
    and eax, ~(1 << 2)
    or eax, 1 << 1
    mov cr0, eax

    ; EFER.LME.
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    ; Paging on (PE already set).
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    jmp dword 0x18:long64

; ---------------- 64-bit ----------------
BITS 64
long64:
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, STACK64
    mov rax, PAYLOAD
    call rax
.halt:
    cli
    hlt
    jmp .halt

; ---------------- data ----------------
BITS 16
align 8
gdt:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF   ; 0x08 code32
    dq 0x00CF92000000FFFF   ; 0x10 data32
    dq 0x00209A0000000000   ; 0x18 code64
    dq 0x0000920000000000   ; 0x20 data64
gdt_ptr:
    dw 8 * 5 - 1
    dd gdt

msg_stage2: db 'KAM stage2', 10, 0
msg_a20:    db 'A20 OK', 10, 0
msg_e820:   db 'E820 done', 10, 0
msg_long:   db 'Entering long mode', 10, 0

TIMES 2048 - ($ - $$) db 0

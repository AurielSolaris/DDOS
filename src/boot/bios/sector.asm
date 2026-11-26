bits 16
org 0x7c00

RESERVED_SECTORS equ 105

jmp short boot
nop
db 'DDOS    '
dw 512
db 1
dw RESERVED_SECTORS
db 2
dw 224
dw 2880
db 0xf0
dw 9
dw 18
dw 2
dd 0, 0
db 0, 0, 0x29
dd 0x44444f53
db 'DDOS       ', 'FAT12   '

boot:
    jmp 0:.normalized
.normalized:
    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 0x7c00
    sti
    cld
    mov [drive], dl
    mov ax, 0x1000
    mov es, ax
    xor bx, bx
    mov si, 1
.next:
    ; Convert the reserved-sector LBA to 1.44 MB floppy CHS.
    mov ax, si
    xor dx, dx
    mov cx, 18
    div cx
    mov cl, dl
    inc cl
    mov dh, al
    and dh, 1
    shr ax, 1
    mov ch, al
    mov dl, [drive]
    mov ax, 0x0201
    int 0x13
    jc disk_error
    add bx, 512
    inc si
    cmp si, RESERVED_SECTORS
    jb .next
    mov dl, [drive]
    jmp 0x1000:0

disk_error:
    mov si, error_message
.print:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x0e
    mov bx, 0x0007
    int 0x10
    jmp .print
.halt:
    cli
    hlt
    jmp .halt

drive db 0
error_message db 'DDOS: boot disk read failed', 0
times 510 - ($ - $$) db 0
dw 0xaa55

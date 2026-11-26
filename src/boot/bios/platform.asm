bits 16
section .entry progbits alloc exec nowrite align=1
global _start
extern kernel_main, __bss_start, __bss_end, load_protected_idt, restore_protected_idt
extern dos_files_begin, dos_file_interrupt, x86_domains_configure, x86_native_configure

KERNEL_BASE equ 0x10000
KERNEL_SEGMENT equ KERNEL_BASE >> 4
PROGRAM_SEGMENT equ 0x3000
DOMAIN_ARENA_OFFSET equ 0x30000
CODE32 equ 0x08
DATA32 equ 0x10
CODE16 equ 0x18
DATA16 equ 0x20
PROGRAM_DATA equ 0x28

_start:
    cli
    cld
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0xfff0
    xor eax, eax
    mov di, __bss_start
    mov cx, __bss_end
    sub cx, di
    rep stosb

    mov [boot_drive], dl
    push ds
    int 0x12
    pop ds
    movzx eax, ax
    shl eax, 10
    mov [conventional_end], eax
    mov ah, 0x88
    push ds
    int 0x15
    pop ds
    jc .no_extended
    cmp ax, 27840              ; 27 MiB of task and staging images plus 192 KiB of metadata.
    jb .no_extended
    mov byte [extended_available], 1
.no_extended:
    xor ax, ax
    mov es, ax
    mov word [es:0x20 * 4], terminate
    mov [es:0x20 * 4 + 2], cs
    mov word [es:0x21 * 4], dos_interrupt
    mov [es:0x21 * 4 + 2], cs
    in al, 0x92
    and al, 0xfe
    or al, 2
    out 0x92, al

    lgdt [gdtr]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword CODE32:protected_entry

section .text progbits alloc exec nowrite align=1
bits 32
global platform_putc, platform_getc, platform_try_getc, platform_clear, platform_read
global platform_ram_kib
global platform_has_mouse, platform_mouse_pending, platform_mouse_read
global platform_can_run, platform_calculator_boot, copy_to_program, copy_from_program, run_program

protected_entry:
    mov ax, DATA32
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov eax, KERNEL_BASE
    call load_protected_idt
    mov eax, [conventional_end]
    sub eax, KERNEL_BASE + DOMAIN_ARENA_OFFSET
    jc .kernel
    sub esp, 4
    push eax
    push dword DOMAIN_ARENA_OFFSET
    push dword KERNEL_BASE
    call x86_domains_configure
    add esp, 16
    cmp byte [extended_available], 0
    je .kernel
    sub esp, 8
    push dword 0x100000 - KERNEL_BASE
    call x86_native_configure
    add esp, 12
.kernel:
    call kernel_main
.halt:
    hlt
    jmp .halt

platform_putc:
    mov eax, [esp + 4]
    mov ecx, putc
    jmp call_bios

platform_getc:
    mov ecx, getc
    jmp call_bios

platform_try_getc:
    mov ecx, try_getc
    jmp call_bios

platform_clear:
    mov ecx, clear
    jmp call_bios

platform_read:
    mov eax, [esp + 4]
    mov edx, [esp + 8]
    mov ecx, read
    jmp call_bios

platform_ram_kib:
platform_has_mouse:
platform_mouse_pending:
platform_mouse_read:
platform_calculator_boot:
    xor eax, eax
    ret

platform_can_run:
    mov eax, 1
    ret

copy_to_program:
    push esi
    push edi
    push es
    mov ax, PROGRAM_DATA
    mov es, ax
    mov edi, [esp + 16]
    mov esi, [esp + 20]
    mov ecx, [esp + 24]
    rep movsb
    pop es
    pop edi
    pop esi
    ret

copy_from_program:
    push esi
    push edi
    push ds
    mov ax, PROGRAM_DATA
    mov ds, ax
    mov edi, [esp + 16]
    mov esi, [esp + 20]
    mov ecx, [esp + 24]
    rep movsb
    pop ds
    pop edi
    pop esi
    ret

run_program:
    mov edx, [esp + 8]
    mov [program_entry], edx
    mov edx, [esp + 12]
    mov [program_initial_stack], edx
    mov eax, [esp + 4]
    sub esp, 8
    push eax
    call dos_files_begin
    add esp, 12
    mov ecx, enter_program

call_bios:
    pushad
    jmp CODE16:protected16

bits 16
protected16:
    mov si, DATA16
    mov ds, si
    mov es, si
    mov fs, si
    mov gs, si
    mov ss, si
    mov esi, cr0
    and esi, ~1
    mov cr0, esi
    jmp KERNEL_SEGMENT:real_entry

real_entry:
    mov si, cs
    mov ds, si
    mov es, si
    mov fs, si
    mov gs, si
    mov ss, si
    lidt [real_idtr]
    sti
    call cx
    cli
    cld
    mov esi, cr0
    or esi, 1
    mov cr0, esi
    jmp dword CODE32:protected_return

bits 32
protected_return:
    mov si, DATA32
    mov ds, si
    mov es, si
    mov fs, si
    mov gs, si
    mov ss, si
    mov [esp + 28], eax
    call restore_protected_idt
    popad
    ret

bits 16
putc:
    out 0xe9, al
    pushad
    push ds
    push es
    mov ah, 0x0e
    mov bx, 0x0007
    int 0x10
    pop es
    pop ds
    popad
    ret

getc:
    xor ax, ax
    int 0x16
    movzx eax, ax
    ret

try_getc:
    mov ah, 1
    int 0x16
    jnz getc
    xor eax, eax
    ret

clear:
    mov ax, 0x0003
    int 0x10
    ret

read:
    mov bx, dx
    xor dx, dx
    mov cx, 18
    div cx
    mov cl, dl
    inc cl
    mov dh, al
    and dh, 1
    shr ax, 1
    mov ch, al
    mov dl, [boot_drive]
    mov ax, 0x0201
    int 0x13
    setnc al
    movzx eax, al
    ret

enter_program:
    cli
    mov [program_stack], esp
    mov ax, PROGRAM_SEGMENT
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    movzx esp, word [cs:program_initial_stack]
    mov ss, [cs:program_initial_stack + 2]
    xor ax, ax
    sti
    jmp far [cs:program_entry]

; INT 20h and INT 21h/4Ch abandon the child's interrupt frame.
terminate:
    xor al, al
exit_program:
    cli
    mov dx, cs
    mov ds, dx
    mov es, dx
    mov ss, dx
    mov fs, dx
    mov gs, dx
    mov esp, [program_stack]
    movzx eax, al
    ret

dos_interrupt:
    cmp ah, 0x4c
    je exit_program
    cmp ah, 0x00
    je terminate
    cmp ah, 0x02
    je .character
    cmp ah, 0x09
    je .string
    cmp ah, 0x30
    je .version
    jmp dos_file_gate
.character:
    mov al, dl
    call putc
    iret
.string:
    push si
    mov si, dx
.next:
    mov al, [si]
    cmp al, '$'
    je .done
    call putc
    inc si
    cmp si, dx
    jne .next
.done:
    pop si
    iret
.version:
    mov ax, 0x0004
    xor bx, bx
    xor cx, cx
    iret

; Copy the child's register frame, then run file I/O on the protected kernel stack.
dos_file_gate:
    push gs
    push fs
    push ds
    push es
    pusha
    mov [cs:child_sp], sp
    mov ax, ss
    mov [cs:child_ss], ax
    mov ds, ax
    mov ax, cs
    mov es, ax
    mov si, sp
    mov di, dos_frame
    mov cx, 15
    cld
    rep movsw
    mov ds, ax
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword CODE32:dos_file_protected

bits 32
dos_file_protected:
    mov ax, DATA32
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, [program_stack]
    and esp, -16
    call restore_protected_idt
    sub esp, 12
    push dword dos_frame
    call dos_file_interrupt
    jmp CODE16:dos_file_leave

bits 16
dos_file_leave:
    mov ax, DATA16
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov eax, cr0
    and eax, ~1
    mov cr0, eax
    jmp KERNEL_SEGMENT:dos_file_resume

dos_file_resume:
    mov ax, cs
    mov ds, ax
    lidt [real_idtr]
    mov ax, [child_ss]
    mov es, ax
    mov di, [child_sp]
    mov si, dos_frame
    mov cx, 15
    cld
    rep movsw
    mov ss, ax
    mov sp, [child_sp]
    popa
    pop es
    pop ds
    pop fs
    pop gs
    iret

section .rodata
align 8
gdt:
    dq 0
    dq 0x00cf9a010000ffff
    dq 0x00cf92010000ffff
    dq 0x00009a010000ffff
    dq 0x000092010000ffff
    dq 0x004092030000ffff
gdt_end:
gdtr:
    dw gdt_end - gdt - 1
    dd KERNEL_BASE + gdt
real_idtr:
    dw 0x3ff
    dd 0

section .bss nobits alloc noexec write align=4
conventional_end resd 1
extended_available resb 1
program_stack resd 1
program_entry resd 1
program_initial_stack resd 1
child_ss resw 1
child_sp resw 1
dos_frame resw 15
boot_drive resb 1
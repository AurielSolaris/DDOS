bits 64
section .entry progbits alloc exec nowrite align=1
global _start
extern uefi_kernel_main, load_protected_idt

CODE32 equ 0x08
DATA32 equ 0x10
FLAT_CODE equ 0x18
FLAT_DATA equ 0x20

_start:
    cli
    cld
    ; RCX is the physical base, identity mapped by x64 UEFI.
    mov edi, ecx
    lea rsp, [rcx + 0xfff0]
    lea rdx, [rel gdt]
    mov [rel gdtr + 2], rdx
    mov [rdx + CODE32 + 2], cx
    mov [rdx + DATA32 + 2], cx
    shr ecx, 16
    mov [rdx + CODE32 + 4], cl
    mov [rdx + DATA32 + 4], cl
    mov [rdx + CODE32 + 7], ch
    mov [rdx + DATA32 + 7], ch
    lgdt [rel gdtr]
    push qword FLAT_CODE
    lea rax, [rel compatibility_entry]
    push rax
    retfq

bits 32
compatibility_entry:
    mov ax, FLAT_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov eax, cr4
    and eax, ~(1 << 17)   ; PCIDE is incompatible with disabling paging.
    mov cr4, eax
    mov eax, cr0
    and eax, ~(1 << 31)
    mov cr0, eax
    mov ecx, 0xc0000080  ; EFER
    rdmsr
    and eax, ~(1 << 8)   ; LME
    wrmsr
    mov eax, cr4
    and eax, ~((1 << 5) | (1 << 12))  ; PAE and LA57
    mov cr4, eax
    jmp far [edi + kernel_entry_pointer]

kernel_entry:
    mov ax, DATA32
    mov ds, ax
    mov es, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0xfff0
    mov ax, FLAT_DATA
    mov fs, ax           ; Physical address space for framebuffer access.
    mov eax, edi
    call load_protected_idt
    sub esp, 12
    push edi
    call uefi_kernel_main
.halt:
    hlt
    jmp .halt

section .data
align 8
gdt:
    dq 0
    dq 0x00cf9a000000ffff, 0x00cf92000000ffff
    dq 0x00cf9a000000ffff, 0x00cf92000000ffff
gdt_end:
gdtr:
    dw gdt_end - gdt - 1
    dq 0
kernel_entry_pointer:
    dd kernel_entry
    dw CODE32
bits 32
section .text
global load_protected_idt, restore_protected_idt

; EAX is the physical base of the kernel segment.
load_protected_idt:
    push edi
    mov edi, idt
    add eax, edi
    mov [idtr + 2], eax
    mov eax, fault
    mov edx, eax
    and eax, 0xffff
    or eax, 0x00080000
    and edx, 0xffff0000
    or edx, 0x8e00
    mov ecx, 32
.install:
    stosd
    xchg eax, edx
    stosd
    xchg eax, edx
    loop .install
    pop edi
restore_protected_idt:
    lidt [idtr]
    ret

fault:
    cli
    mov al, '!'
    out 0xe9, al
.halt:
    hlt
    jmp .halt

section .data
idtr:
    dw 32 * 8 - 1
    dd 0

section .bss
alignb 8
idt resq 32
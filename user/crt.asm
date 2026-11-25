bits 32
section .text.start progbits alloc exec nowrite align=16
global _start
extern main

_start:
    cld
    mov eax, [esp]
    lea ecx, [esp + 4]
    lea edx, [ecx + eax * 4 + 4]
    sub esp, 4
    push edx
    push ecx
    push eax
    call main
    mov ebx, eax
    mov eax, 1
    int 0x80
    ud2

section .text
global ddos_syscall

ddos_syscall:
    push ebp
    push edi
    push esi
    push ebx
    mov eax, [esp + 20]
    mov ebx, [esp + 24]
    mov ecx, [esp + 28]
    mov edx, [esp + 32]
    mov esi, [esp + 36]
    mov edi, [esp + 40]
    mov ebp, [esp + 44]
    int 0x80
    pop ebx
    pop esi
    pop edi
    pop ebp
    ret
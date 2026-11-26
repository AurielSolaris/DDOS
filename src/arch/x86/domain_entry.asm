bits 32
section .text
global x86_has_write_protect

x86_has_write_protect:
    pushfd
    pop edx
    mov eax, edx
    xor eax, 1 << 18      ; The AC flag is fixed on the 386.
    push eax
    popfd
    pushfd
    pop eax
    push edx
    popfd
    xor eax, edx
    and eax, 1 << 18
    ret

global x86_enable_sse
x86_enable_sse:
    pushfd
    pop ecx
    mov eax, ecx
    xor eax, 1 << 21     ; CPUID is optional on older 32-bit CPUs.
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    xor eax, ecx
    test eax, 1 << 21
    jz .unsupported
    push ebx
    mov eax, 1
    cpuid
    pop ebx
    and edx, (1 << 24) | (1 << 25)   ; FXSR and SSE
    cmp edx, (1 << 24) | (1 << 25)
    jne .unsupported
    mov eax, cr0
    and eax, ~((1 << 2) | (1 << 3))  ; EM and TS
    or eax, 1 << 1                  ; MP
    mov cr0, eax
    mov eax, cr4
    or eax, (1 << 9) | (1 << 10)     ; OSFXSR and OSXMMEXCPT
    mov cr4, eax
    mov eax, 1
    ret
.unsupported:
    xor eax, eax
    ret

global x86_save_fpu, x86_restore_fpu
x86_save_fpu:
    mov eax, [esp + 4]
    fxsave [eax]
    ret

x86_restore_fpu:
    mov eax, [esp + 4]
    fxrstor [eax]
    ret

section .domain_bridge progbits alloc exec nowrite align=16
global x86_enter_domain, domain_syscall, domain_gp, domain_pf, domain_ud, domain_bad, domain_bad_error, domain_fatal
extern x86_domain_gdtr, x86_domain_idtr, __domain_stack_top

KERNEL_DATA equ 0x10
TSS_SELECTOR equ 0x50

; All kernel selectors, the IDT, and both stacks stay mapped during the switch.
x86_enter_domain:
    push ds
    push es
    push fs
    push gs
    pushad
    mov [kernel_stack], esp
    mov eax, cr0
    mov [kernel_cr0], eax
    mov edx, cr3
    mov [kernel_cr3], edx
    mov eax, [esp + 56]
    mov [service_mode], eax
    mov eax, [esp + 60]
    mov [user_frame], eax
    sgdt [kernel_gdtr]
    sidt [kernel_idtr]
    lgdt [x86_domain_gdtr]
    lidt [x86_domain_idtr]
    mov dx, TSS_SELECTOR
    ltr dx
    mov edx, [esp + 52]
    mov cr3, edx

    mov esi, [user_frame]
    mov esp, __domain_stack_top
    mov edx, [esi + 48]
    push edx
    push dword [esi + 44]
    mov eax, [esi + 40]
    and eax, 0x0cd5           ; Do not restore privileged EFLAGS bits.
    or eax, 2
    push eax
    push dword [esi + 36]
    push dword [esi + 32]
    push dword [esi + 28]
    push dword [esi + 24]
    push dword [esi + 20]
    push dword [esi + 16]
    push dword 0
    push dword [esi + 8]
    push dword [esi + 4]
    push dword [esi]
    push dword [esi + 56]
    push dword [esi + 52]
    mov eax, cr0
    or eax, (1 << 31) | (1 << 16)  ; Paging and supervisor write protection.
    mov cr0, eax
    mov ds, dx
    mov es, dx
    pop fs
    pop gs
    popad
    iretd

; Native proof-of-concept ABI: EAX=0 asks for the caller's CPL.
; EAX=1 exits the domain with the value in EBX. Other calls return -ENOSYS.
domain_syscall:
    test byte [esp + 4], 3
    jz domain_fatal
    cmp dword [cs:service_mode], 0
    jne native_syscall
    cmp eax, 1
    je .exit
    test eax, eax
    jnz .unsupported
    mov eax, [esp + 4]
    and eax, 3
    iretd
.unsupported:
    mov eax, -38
    iretd
.exit:
    mov eax, ebx
    jmp domain_return

native_syscall:
    pushad
    mov esi, esp
    xor eax, eax
    mov ax, fs
    push eax
    mov ax, gs
    push eax
    mov dx, KERNEL_DATA
    mov ds, dx
    mov es, dx
    mov edx, [kernel_cr0]
    mov cr0, edx
    mov edi, [user_frame]
    mov ecx, 13
    cld
    rep movsd
    pop eax
    mov [edi + 4], eax
    pop eax
    mov [edi], eax
    mov eax, 0x40000000
    jmp domain_return

domain_bad:
    push dword 0
domain_bad_error:
    mov eax, 0x800000ff
    jmp domain_fault

domain_ud:
    push dword 0
    mov eax, 0x80000006
    jmp domain_fault
domain_gp:
    mov eax, 0x8000000d
    jmp domain_fault
domain_pf:
    mov eax, 0x8000000e
domain_fault:
    test byte [esp + 8], 3
    jz domain_fatal

domain_return:
    cld
    mov dx, KERNEL_DATA
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx
    mov edx, [kernel_cr0]
    mov cr0, edx
    mov edx, [kernel_cr3]
    mov cr3, edx
    lgdt [kernel_gdtr]
    lidt [kernel_idtr]
    mov esp, [kernel_stack]
    mov [esp + 28], eax
    popad
    pop gs
    pop fs
    pop es
    pop ds
    ret

domain_fatal:
    cli
    mov al, '!'
    out 0xe9, al
.halt:
    hlt
    jmp .halt

; Every entry is copied into each domain's distinct, read-only code page.
section .rodata
global x86_payload_start, x86_payload_end
global x86_driver_probe, x86_driver_cli
global x86_user_probe, x86_user_port, x86_user_kernel_segment
global x86_user_driver_segment, x86_user_kernel_offset, x86_user_unmapped
global x86_user_bridge_write
x86_payload_start:
x86_driver_probe:
    xor eax, eax
    int 0x80
    cmp eax, 1
    jne failed
    mov [0], eax
    in al, 0x80
    movzx ebx, al
    or ebx, 0x100
    jmp exit_domain

x86_driver_cli:
    cli
    jmp failed

x86_user_probe:
    cmp dword [0], 0
    jne failed
    xor eax, eax
    int 0x80
    mov [0], eax
    mov ebx, eax
    jmp exit_domain

x86_user_port:
    in al, 0x80
    jmp failed

x86_user_kernel_segment:
    mov ax, KERNEL_DATA
    mov ds, ax
    jmp failed

x86_user_driver_segment:
    mov ax, 0x39
    mov ds, ax
    jmp failed

x86_user_kernel_offset:
    mov eax, [esi]
    jmp failed

x86_user_unmapped:
    mov eax, [4096]
    jmp failed

x86_user_bridge_write:
    mov dword [esi], 0xbad
failed:
    mov ebx, 0x0bad
exit_domain:
    mov eax, 1
    int 0x80
    ud2
x86_payload_end:

section .domain_state nobits alloc noexec write align=4
user_frame resd 1
service_mode resd 1
kernel_stack resd 1
kernel_cr0 resd 1
kernel_cr3 resd 1
kernel_gdtr resb 6
kernel_idtr resb 6
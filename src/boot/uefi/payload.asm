bits 64

section .rdata align=16
global kernel_start, kernel_end, disk_start, disk_end

kernel_start:
    incbin "build/uefi-kernel.bin"
kernel_end:
disk_start:
    incbin "build/ddos.img"
disk_end:
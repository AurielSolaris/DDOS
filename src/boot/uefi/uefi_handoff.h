#ifndef DDOS_UEFI_HANDOFF_H
#define DDOS_UEFI_HANDOFF_H

#include <stdint.h>

/* Offsets from the base of the kernel's preallocated memory. */
#define DDOS_HANDOFF_OFFSET 0x10000u
#define DDOS_DISK_OFFSET    0x11000u

struct uefi_handoff {
    uint32_t framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t stride;  /* Pixels per scanline. */
    uint32_t domain_arena;
    uint32_t domain_bytes;
};

#endif
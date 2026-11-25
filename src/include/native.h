#ifndef DDOS_NATIVE_H
#define DDOS_NATIVE_H

#include <stdbool.h>
#include <stdint.h>

enum {
    NATIVE_PAGE = 4096, NATIVE_PAGES = 8,
    NATIVE_FILE_LIMIT = 16 * 1024, NATIVE_TRAP = 0x40000000,
};

/* Matches PUSHAD followed by the inter-privilege IRET frame. */
struct native_frame {
    uint32_t edi, esi, ebp, ignored, ebx, edx, ecx, eax;
    uint32_t eip, cs, flags, esp, ss, fs, gs;
};

struct native_space {
    uint8_t *image, *file;
    uint32_t *pages, directory;
    struct native_frame frame;
};

bool native_space_init(struct native_space *);
void native_tls(uint32_t base, uint32_t limit);
void native_page(struct native_space *, unsigned slot, bool present);
uint32_t native_step(struct native_space *);
void native_start(uint32_t directory, const char *path, bool quiet);

#endif
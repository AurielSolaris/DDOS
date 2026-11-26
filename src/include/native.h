#ifndef DDOS_NATIVE_H
#define DDOS_NATIVE_H

#include <stdbool.h>
#include <stdint.h>

enum {
    NATIVE_PAGE = 4096, NATIVE_PAGES = 256,
    NATIVE_FB_OFFSET = NATIVE_PAGES * NATIVE_PAGE,
    NATIVE_FB_MAX = 8 * 1024 * 1024,
    NATIVE_TRAP = 0x40000000, NATIVE_TIMER = 0x40000001,
};

/* Matches PUSHAD followed by the inter-privilege IRET frame. */
struct native_frame {
    uint32_t edi, esi, ebp, ignored, ebx, edx, ecx, eax;
    uint32_t eip, cs, flags, esp, ss, fs, gs;
};

struct native_space {
    uint8_t *image, *stage;
    uint32_t base, size, tls_base, tls_limit, fb_access;
    uint8_t access[NATIVE_PAGES];
    _Alignas(16) uint8_t fpu[512];
    struct native_frame frame;
};

struct ext2_inode;
struct native_elf {
    uint32_t entry, phdr, phnum, heap_start, stack_bottom;
};

struct native_fb {
    uint32_t physical, width, height, stride, size, red, blue;
};
void native_fb_configure(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
const struct native_fb *native_framebuffer(void);

void *native_init(void);
void native_space_init(struct native_space *, unsigned slot);
void native_reset_fpu(struct native_space *);
void native_page(struct native_space *, unsigned slot, unsigned permissions);
void native_commit(struct native_space *, uint32_t base, uint32_t size);
bool native_load_elf(struct native_space *, const struct ext2_inode *, struct native_elf *);
uint32_t native_step(struct native_space *);
void native_clock_start(void);
void native_clock_stop(void);
void native_clock_domain(bool entering);
uint32_t native_millis(void);
uint32_t native_epoch(void);
void native_desktop(uint32_t directory, const char *app);
void native_start(uint32_t directory, const char *path, bool quiet);

#endif
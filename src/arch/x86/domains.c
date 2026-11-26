#include "x86_domains.h"
#include "native.h"
#include "desktop_status.h"

#include <stdbool.h>

#include "bytes.h"
#include "console.h"

enum {
    PAGE = 4096, ENTRIES = PAGE / sizeof(uint32_t), KERNEL_BYTES = 0x20000,
    DOMAIN_CODE = 0xc0000000u, DOMAIN_DATA = DOMAIN_CODE + PAGE,
    PRESENT = 1, WRITABLE = 2,
    RING1_CODE = 0x30, RING1_DATA = 0x38, RING2_CODE = 0x40, RING2_DATA = 0x48,
    TSS_SELECTOR = 0x50, KERNEL_DATA = 0x10,
    TLS_SELECTOR = 0x58, GDT_BYTES = TLS_SELECTOR + 8, IDT_ENTRIES = 0x81,
    TSS_BYTES = 104, PROBE_PORT = 0x80, BITMAP_BYTES = PROBE_PORT / 8 + 2,
    GP = 0x8000000du, PF = 0x8000000eu,
};

struct domain_memory {
    uint32_t directories[2][ENTRIES];
    uint32_t kernel_tables[2][ENTRIES];
    uint32_t domain_tables[2][ENTRIES];
    uint32_t framebuffer_tables[2][ENTRIES];
    uint8_t code[2][PAGE];
    uint8_t data[2][PAGE];
};

_Static_assert(sizeof(struct domain_memory) == DDOS_X86_DOMAIN_PAGES * PAGE,
               "firmware arena size must match paging layout");
_Static_assert(NATIVE_PAGES == DDOS_X86_TASK_PAGES && NATIVE_MAX_PAGES == DDOS_X86_DESKTOP_PAGES,
               "firmware must reserve task and desktop images at their configured sizes");

struct __attribute__((packed)) gdtr {
    uint16_t limit;
    uint32_t base;
};

static struct domain_memory *memory;
static uint32_t physical_base;
static uint8_t *native_memory;
static bool sse;
static struct native_fb framebuffer;
static const uint32_t kernel_read_probe = 0x3232;
#define DOMAIN_STATE __attribute__((section(".domain_state"), aligned(8)))

static uint8_t tss[TSS_BYTES + BITMAP_BYTES] DOMAIN_STATE;
static uint8_t domain_gdt[GDT_BYTES] DOMAIN_STATE;
static uint32_t domain_idt[IDT_ENTRIES][2] DOMAIN_STATE;
static volatile uint32_t write_probe DOMAIN_STATE;
struct gdtr x86_domain_gdtr DOMAIN_STATE, x86_domain_idtr DOMAIN_STATE;

extern unsigned x86_has_write_protect(void);
extern unsigned x86_enable_sse(void);
extern unsigned x86_has_cpuid(void);
extern void x86_copy_scanline_sse2(uint32_t dest, const void *source, unsigned pixels);
extern void x86_save_fpu(void *);
extern void x86_restore_fpu(const void *);
extern uint32_t x86_enter_domain(uint32_t directory, unsigned service, struct native_frame *);
_Static_assert(sizeof(struct native_frame) == 60, "x86 trap frame layout");
extern uint8_t x86_payload_start[], x86_payload_end[];
extern uint8_t x86_driver_probe[], x86_user_probe[], x86_user_port[];
extern uint8_t x86_user_kernel_segment[], x86_user_kernel_offset[], x86_driver_cli[];
extern uint8_t x86_user_unmapped[], x86_user_driver_segment[], x86_user_bridge_write[];
extern uint8_t domain_fatal[], domain_ud[], domain_gp[], domain_pf[], domain_syscall[];
extern uint8_t domain_bad[], domain_bad_error[];
extern uint8_t __domain_bridge_start[], __domain_stack_top[];

void x86_domains_configure(uint32_t kernel_base, uint32_t arena_offset, uint32_t bytes)
{
    if ((kernel_base | arena_offset) & (PAGE - 1))
        return;
    if (arena_offset < KERNEL_BYTES || bytes < sizeof(struct domain_memory) ||
        arena_offset > UINT32_MAX - kernel_base - sizeof(struct domain_memory))
        return;
    physical_base = kernel_base;
    memory = (struct domain_memory *)(uintptr_t)arena_offset;
}

void x86_native_configure(uint32_t arena_offset)
{
    native_memory = (uint8_t *)(uintptr_t)arena_offset;
}

uint32_t x86_physical_address(const void *offset)
{
    return physical_base + (uint32_t)(uintptr_t)offset;
}

static void descriptor(uint8_t *slot, uint32_t base, unsigned limit,
                       unsigned access, unsigned flags)
{
    write16(slot, limit);
    write16(slot + 2, base);
    slot[4] = base >> 16;
    slot[5] = access;
    slot[6] = flags | (limit >> 16);
    slot[7] = base >> 24;
}

extern uint8_t domain_timer[];

static void gate(unsigned vector, const uint8_t *entry, unsigned attributes)
{
    uint32_t address = (uintptr_t)entry;
    domain_idt[vector][0] = 0x00080000 | (address & 0xffff);
    domain_idt[vector][1] = (address & 0xffff0000) | (attributes << 8);
}

static bool prepare_descriptors(void)
{
    struct gdtr gdt;
    __asm__ volatile ("sgdt %0" : "=m"(gdt));
    if (gdt.limit < KERNEL_DATA + 7 || gdt.base < physical_base ||
        gdt.base - physical_base > KERNEL_BYTES - KERNEL_DATA - 8)
        return false;
    const uint8_t *original = (const uint8_t *)(uintptr_t)(gdt.base - physical_base);
    uint8_t *table = domain_gdt;
    for (unsigned i = 0; i < GDT_BYTES; ++i)
        table[i] = i < KERNEL_DATA + 8 ? original[i] : 0;
    /* Descriptor access bits must already be set on the read-only bridge page. */
    table[0x08 + 5] |= 1;
    table[KERNEL_DATA + 5] |= 1;

    /* Byte-granular, non-conforming code and separate writable data. */
    descriptor(table + RING1_CODE, DOMAIN_CODE, PAGE - 1, 0xbb, 0x40);
    descriptor(table + RING1_DATA, DOMAIN_DATA, PAGE - 1, 0xb3, 0x40);
    descriptor(table + RING2_CODE, DOMAIN_CODE, PAGE - 1, 0xdb, 0x40);
    descriptor(table + RING2_DATA, DOMAIN_DATA, PAGE - 1, 0xd3, 0x40);
    for (unsigned i = 0; i < TSS_BYTES; ++i)
        tss[i] = 0;
    write32(tss + 4, (uintptr_t)__domain_stack_top);
    write16(tss + 8, KERNEL_DATA);
    write16(tss + 102, TSS_BYTES);
    for (unsigned i = TSS_BYTES; i < sizeof(tss); ++i)
        tss[i] = 0xff;
    x86_domain_gdtr.limit = GDT_BYTES - 1;
    x86_domain_gdtr.base = x86_physical_address(domain_gdt);
    x86_domain_idtr.limit = sizeof(domain_idt) - 1;
    x86_domain_idtr.base = x86_physical_address(domain_idt);
    for (unsigned vector = 0; vector < IDT_ENTRIES; ++vector)
        gate(vector, domain_fatal, 0x8e);
    gate(0, domain_bad, 0x8e);
    gate(1, domain_bad, 0x8e);
    gate(3, domain_bad, 0x8e);
    gate(4, domain_bad, 0x8e);
    gate(5, domain_bad, 0x8e);
    gate(6, domain_ud, 0x8e);
    gate(7, domain_bad, 0x8e);
    gate(10, domain_bad_error, 0x8e);
    gate(11, domain_bad_error, 0x8e);
    gate(12, domain_bad_error, 0x8e);
    gate(17, domain_bad_error, 0x8e);
    gate(13, domain_gp, 0x8e);
    gate(14, domain_pf, 0x8e);
    gate(0x20, domain_timer, 0x8e);
    gate(0x80, domain_syscall, 0xce);
    return true;
}

static bool build_address_spaces(void)
{
    unsigned bytes = (uintptr_t)x86_payload_end - (uintptr_t)x86_payload_start;
    if (bytes > PAGE)
        return false;
    for (unsigned i = 0; i < ENTRIES; ++i) {
        for (unsigned d = 0; d < 2; ++d) {
            memory->directories[d][i] = 0;
            memory->kernel_tables[d][i] = 0;
            memory->domain_tables[d][i] = 0;
        }
    }
    uint32_t bridge[] = {x86_physical_address(__domain_bridge_start), x86_physical_address(__domain_stack_top) - PAGE};
    for (unsigned i = 0; i < sizeof(bridge) / sizeof(bridge[0]); ++i) {
        uint32_t address = bridge[i];
        unsigned table = (address >> 22) - (physical_base >> 22);
        unsigned entry = (address >> 12) & (ENTRIES - 1);
        memory->kernel_tables[table][entry] = address | PRESENT | (i == 1 ? WRITABLE : 0);
        for (unsigned d = 0; d < 2; ++d)
            memory->directories[d][address >> 22] =
                x86_physical_address(memory->kernel_tables[table]) | PRESENT | WRITABLE;
    }
    for (unsigned d = 0; d < 2; ++d) {
        memory->directories[d][DOMAIN_CODE >> 22] =
            x86_physical_address(memory->domain_tables[d]) | PRESENT | WRITABLE;
        memory->domain_tables[d][0] = x86_physical_address(memory->code[d]) | PRESENT;
        memory->domain_tables[d][1] = x86_physical_address(memory->data[d]) | PRESENT | WRITABLE;
        for (unsigned i = 0; i < PAGE; ++i) {
            memory->code[d][i] = i < bytes ? x86_payload_start[i] : 0xf4;
            memory->data[d][i] = 0;
        }
    }
    return true;
}

static void prepare_entry(unsigned ring)
{
    uint8_t *trap_stack = (uint8_t *)((uintptr_t)__domain_stack_top - PAGE);
    for (unsigned i = 0; i < PAGE; ++i)
        trap_stack[i] = 0;
    descriptor(domain_gdt + TSS_SELECTOR, x86_physical_address(tss), sizeof(tss) - 1, 0x89, 0);
    tss[TSS_BYTES + PROBE_PORT / 8] =
        ring == 1 ? (uint8_t)~(1u << (PROBE_PORT % 8)) : 0xff;
}

static uint32_t run(unsigned ring, const uint8_t *entry, uint32_t argument)
{
    struct native_frame frame = {
        .esi = argument, .eip = (uintptr_t)entry - (uintptr_t)x86_payload_start,
        .esp = PAGE - 16, .cs = (ring == 1 ? RING1_CODE : RING2_CODE) | ring,
        .ss = (ring == 1 ? RING1_DATA : RING2_DATA) | ring,
    };
    prepare_entry(ring);
    return x86_enter_domain(x86_physical_address(memory->directories[ring - 1]), 0, &frame);
}

void native_fb_configure(uint32_t base, uint32_t shadow, uint32_t width, uint32_t height,
                         uint32_t stride, uint32_t format)
{
    if (!base || (base & (PAGE - 1)) || !height || stride > (NATIVE_FB_MAX / 4) / height)
        return;
    unsigned size = stride * height * 4;
    if (!size || (size & (PAGE - 1)) || base > UINT32_MAX - (size - 1))
        return;
    framebuffer = (struct native_fb){base, shadow, width, height, stride, size,
                                     format ? 16 : 0, format ? 0 : 16};
}

const struct native_fb *native_framebuffer(void)
{
    return framebuffer.size ? &framebuffer : 0;
}

static void cpuid(uint32_t leaf, uint32_t words[4])
{
    __asm__ volatile ("cpuid" : "=a"(words[0]), "=b"(words[1]), "=c"(words[2]), "=d"(words[3]) : "a"(leaf));
}

static uint32_t pci_read(unsigned slot, unsigned offset)
{
    uint32_t address = 0x80000000u | (slot << 8) | offset, value;
    __asm__ volatile ("outl %0, %w1" :: "a"(address), "d"(0xcf8));
    __asm__ volatile ("inl %w1, %0" : "=a"(value) : "d"(0xcfc));
    return value;
}

void native_graphics_detect(struct ddos_graphics_status *status)
{
    if (framebuffer.shadow)
        status->capabilities |= DDOS_GRAPHICS_SHADOW;
    if (x86_has_cpuid()) {
        uint32_t words[4];
        cpuid(0, words);
        uint32_t max = words[0];
        write32((uint8_t *)status->cpu, words[1]);
        write32((uint8_t *)status->cpu + 4, words[3]);
        write32((uint8_t *)status->cpu + 8, words[2]);
        if (sse && max >= 1) {
            cpuid(1, words);
            if (words[3] & (1u << 26))
                status->capabilities |= DDOS_GRAPHICS_SSE2;
        }
        cpuid(0x80000000u, words);
        if (words[0] >= 0x80000004u) {
            for (unsigned leaf = 0; leaf < 3; ++leaf) {
                cpuid(0x80000002u + leaf, words);
                for (unsigned i = 0; i < 4; ++i)
                    write32((uint8_t *)status->cpu + leaf * 16 + i * 4, words[i]);
            }
        }
    }
    for (unsigned slot = 0; slot < 32 * 8; ++slot) {
        if ((pci_read(slot, 8) >> 24) != 3)
            continue;
        uint32_t id = pci_read(slot, 0);
        status->pci_vendor = id;
        status->pci_device = id >> 16;
        break;
    }
}

void native_fb_present(unsigned x, unsigned y, unsigned width, unsigned height, bool simd)
{
    unsigned offset = (y * framebuffer.stride + x) * 4;
    const uint8_t *source = (const void *)(framebuffer.shadow - physical_base + offset);
    uint32_t target = framebuffer.physical - physical_base + offset;
    unsigned pitch = framebuffer.stride * 4;
    while (height--) {
        const uint8_t *row = source;
        uint32_t dest = target;
        unsigned count = width;
        if (simd) {
            x86_copy_scanline_sse2(dest, row, count);
        } else {
            __asm__ volatile ("cld; rep movsl" : "+S"(row), "+D"(dest), "+c"(count) :: "memory");
        }
        source += pitch;
        target += pitch;
    }
    if (simd)
        __asm__ volatile ("sfence" ::: "memory");
}

void *native_init(void)
{
    if (!memory || !native_memory || !x86_has_write_protect() ||
        !prepare_descriptors() || !build_address_spaces())
        return 0;
    sse = x86_enable_sse();
    return native_memory + (DDOS_X86_IMAGE_PAGES + DDOS_X86_DESKTOP_PAGES) * PAGE;
}

void native_space_init(struct native_space *app, unsigned slot, bool desktop_server)
{
    unsigned pages = slot ? NATIVE_MAX_PAGES + (slot - 1) * NATIVE_PAGES : 0;
    app->image = native_memory + pages * PAGE;
    app->stage = native_memory + DDOS_X86_IMAGE_PAGES * PAGE;
    app->capacity = (desktop_server ? NATIVE_MAX_PAGES : NATIVE_PAGES) * PAGE;
    app->frame.cs = RING2_CODE | 2;
    app->frame.ss = RING2_DATA | 2;
}

void native_reset_fpu(struct native_space *app)
{
    for (unsigned i = 0; i < sizeof(app->fpu); ++i)
        app->fpu[i] = 0;
    write16(app->fpu, 0x37f);
    write32(app->fpu + 24, 0x1f80);
}

void native_page(struct native_space *app, unsigned slot, unsigned permissions)
{
    app->access[slot] = permissions;
}

void native_commit(struct native_space *app, uint32_t base, uint32_t size)
{
    for (unsigned i = 0; i < size; ++i)
        app->image[i] = app->stage[i];
    app->base = base;
    app->size = size;
}

uint32_t native_step(struct native_space *app)
{
    uint32_t limit = (app->base + app->size - 1) >> 12;
    descriptor(domain_gdt + RING2_CODE, DOMAIN_CODE - app->base, limit, 0xdb, 0xc0);
    uint32_t data_limit = app->fb_access ?
        (app->base + app->capacity + framebuffer.size - 1) >> 12 : limit;
    descriptor(domain_gdt + RING2_DATA, DOMAIN_CODE - app->base, data_limit, 0xd3, 0xc0);
    unsigned tls_flags = app->tls_limit > 0xfffff ? 0xc0 : 0x40;
    descriptor(domain_gdt + TLS_SELECTOR, DOMAIN_CODE + app->tls_base,
               tls_flags == 0xc0 ? app->tls_limit >> 12 : app->tls_limit, 0xd3, tls_flags);
    unsigned image_pages = app->capacity / PAGE;
    for (unsigned slot = 0; slot < image_pages; ++slot) {
        unsigned access = app->access[slot];
        memory->domain_tables[1][slot] = access ? x86_physical_address(app->image + slot * PAGE) | access : 0;
    }
    uint32_t fb_base = framebuffer.physical;
    unsigned fb_flags = app->fb_access | 0x18;
    if (app->fb_access & NATIVE_FB_SHADOW) {
        fb_base = framebuffer.shadow;
        fb_flags = app->fb_access & 3;
    }
    for (unsigned slot = image_pages; slot < ENTRIES; ++slot) {
        uint32_t offset = (slot - image_pages) * PAGE;
        memory->domain_tables[1][slot] = app->fb_access && offset < framebuffer.size ?
            (fb_base + offset) | fb_flags : 0;
    }
    for (unsigned table = 0; table < 2; ++table) {
        unsigned start = (table + 1) * ENTRIES * PAGE - app->capacity;
        uint32_t *pages = memory->framebuffer_tables[table];
        uint32_t *directory = &memory->directories[1][(DOMAIN_CODE >> 22) + table + 1];
        *directory = 0;
        if (!app->fb_access || framebuffer.size <= start)
            continue;
        for (unsigned i = 0; i < ENTRIES; ++i) {
            unsigned offset = start + i * PAGE;
            pages[i] = offset < framebuffer.size ?
                (fb_base + offset) | fb_flags : 0;
        }
        *directory = x86_physical_address(pages) | PRESENT | WRITABLE;
    }
    prepare_entry(2);
    if (sse)
        x86_restore_fpu(app->fpu);
    native_clock_domain(true);
    uint32_t event = x86_enter_domain(x86_physical_address(memory->directories[1]), 1, &app->frame);
    native_clock_domain(false);
    if (sse)
        x86_save_fpu(app->fpu);
    return event;
}

void x86_domains_test(void)
{
    if (!memory || !x86_has_write_protect() || !prepare_descriptors() || !build_address_spaces()) {
        print("Privilege test unavailable\r\n");
        return;
    }

    uint32_t driver = run(1, x86_driver_probe, 0);
    bool driver_ok = (driver & ~0xffu) == 0x100 && read32(memory->data[0]) == 1;
    bool cli_blocked = run(1, x86_driver_cli, 0) == GP;
    uint32_t driver_kernel = run(1, x86_user_kernel_offset, physical_base - DOMAIN_DATA);
    bool driver_memory = driver_kernel == GP || driver_kernel == PF;
    print(driver_ok && cli_blocked && driver_memory ?
          "Ring 1: port enabled, CLI denied, kernel unmapped\r\n" :
                                     "Ring 1: privilege test failed\r\n");

    uint32_t user = run(2, x86_user_probe, 0);
    bool private_data = user == 2 && read32(memory->data[0]) == 1 &&
                        read32(memory->data[1]) == 2;
    bool io_blocked = run(2, x86_user_port, 0) == GP;
    bool kernel_selector = run(2, x86_user_kernel_segment, 0) == GP;
    bool driver_selector = run(2, x86_user_driver_segment, 0) == GP;
    uint32_t unreachable = run(2, x86_user_unmapped, 0);
    bool guard = unreachable == PF || unreachable == GP;
    uint32_t kernel = run(2, x86_user_kernel_offset, physical_base - DOMAIN_DATA);
    uint32_t kernel_data = run(2, x86_user_kernel_offset,
                               x86_physical_address(&kernel_read_probe) - DOMAIN_DATA);
    write_probe = 0x1960;
    uint32_t bridge = run(2, x86_user_bridge_write,
                          x86_physical_address((const void *)&write_probe) - DOMAIN_DATA);
    bool kernel_memory = (kernel == GP || kernel == PF) &&
                         (kernel_data == GP || kernel_data == PF) &&
                         (bridge == GP || bridge == PF) && write_probe == 0x1960;
    print(private_data && io_blocked && kernel_selector && driver_selector && guard ?
          "Ring 2: syscall, private pages, and I/O restrictions passed\r\n" :
          "Ring 2: privilege test failed\r\n");
    print(kernel_memory ? "Ring 2: kernel unmapped, bridge write denied\r\n" :
                          "Ring 2: kernel isolation test failed\r\n");
}
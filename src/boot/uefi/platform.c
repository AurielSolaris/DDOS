#include <stdbool.h>
#include <stdint.h>

#include "uefi_handoff.h"
#include "x86_domains.h"
#include "native.h"
#include "font8x8_basic.h"
#include "ps2.h"

extern void kernel_main(void);

static const struct uefi_handoff *const video = (const void *)DDOS_HANDOFF_OFFSET;
static unsigned x, y;

static void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void pixel(uint32_t address, uint32_t color)
{
    __asm__ volatile ("movl %0, %%fs:(%1)" :: "r"(color), "r"(address) : "memory");
}

static uint32_t read_pixel(uint32_t address)
{
    uint32_t value;
    __asm__ volatile ("movl %%fs:(%1), %0" : "=r"(value) : "r"(address) : "memory");
    return value;
}

enum { CELL_WIDTH = 8, CELL_HEIGHT = 16 };

static void scroll(void)
{
    uint32_t base = video->framebuffer;
    uint32_t pitch = video->stride * 4;
    unsigned lines = video->height / CELL_HEIGHT * CELL_HEIGHT;
    for (unsigned row = 0; row < lines; ++row) {
        for (unsigned col = 0; col < video->width; ++col) {
            uint32_t address = base + row * pitch + col * 4;
            uint32_t color = 0;
            if (row + CELL_HEIGHT < lines)
                color = read_pixel(address + CELL_HEIGHT * pitch);
            pixel(address, color);
        }
    }
}

void platform_clear(void)
{
    for (unsigned row = 0; row < video->height; ++row) {
        for (unsigned col = 0; col < video->width; ++col)
            pixel(video->framebuffer + (row * video->stride + col) * 4, 0);
    }
    x = y = 0;
}

static void newline(void)
{
    y += CELL_HEIGHT;
    if (y + CELL_HEIGHT > video->height) {
        scroll();
        y -= CELL_HEIGHT;
    }
}

void platform_putc(unsigned character)
{
    outb(0xe9, character);
    if (character == '\r') {
        x = 0;
    } else if (character == '\n') {
        newline();
    } else if (character == '\b') {
        if (x)
            x -= CELL_WIDTH;
    } else if (character >= 32) {
        unsigned glyph = character < 127 ? character : '?';
        for (unsigned row = 0; row < CELL_HEIGHT; ++row) {
            uint8_t bits = font8x8_basic[glyph][row / 2];
            for (unsigned bit = 0; bit < CELL_WIDTH; ++bit) {
                uint32_t address = video->framebuffer +
                    ((y + row) * video->stride + x + bit) * 4;
                pixel(address, (bits & (1u << bit)) ? 0x00ffffff : 0);
            }
        }
        x += CELL_WIDTH;
        if (x + CELL_WIDTH > video->width) {
            x = 0;
            newline();
        }
    }
}

bool platform_read(unsigned sector, void *buffer)
{
    if (sector >= 2880)
        return false;
    const uint8_t *source = (const void *)(DDOS_DISK_OFFSET + sector * 512);
    uint8_t *target = buffer;
    for (unsigned offset = 0; offset < 512; ++offset)
        target[offset] = source[offset];
    return true;
}

static void firmware_select(uint16_t item)
{
    __asm__ volatile ("outw %0,%1" :: "a"(item), "d"((uint16_t)0x510));
}

static uint32_t firmware_read(unsigned bytes)
{
    uint32_t value = 0;
    while (bytes--)
        value = (value << 8) | inb(0x511);
    return value;
}

uint32_t platform_ram_kib(void)
{
    firmware_select(0);
    if (firmware_read(4) != 0x51454d55)
        return 0;
    firmware_select(3); /* QEMU little-endian RAM size in bytes. */
    uint32_t low = 0, high = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
        low |= firmware_read(1) << shift;
    for (unsigned shift = 0; shift < 32; shift += 8)
        high |= firmware_read(1) << shift;
    return high >= 1024 ? UINT32_MAX : (high << 22) | (low >> 10);
}

bool platform_calculator_boot(void)
{
    static const char option[] = "opt/ddos/calculator";
    firmware_select(0);
    if (firmware_read(4) != 0x51454d55) /* QEMU */
        return false;
    firmware_select(0x19);
    unsigned count = firmware_read(4);
    if (count > 0x4000)
        return false;
    for (unsigned i = 0; i < count; ++i) {
        unsigned size = firmware_read(4), selector = firmware_read(2);
        firmware_read(2);
        bool match = true;
        for (unsigned j = 0; j < 56; ++j) {
            unsigned ch = firmware_read(1);
            if (j < sizeof(option) && ch != (unsigned char)option[j])
                match = false;
        }
        if (match && size && selector >= 0x20 && selector < 0x4000) {
            firmware_select(selector);
            return firmware_read(1) == '1';
        }
    }
    return false;
}

bool platform_can_run(void)
{
    return false;
}

void copy_to_program(unsigned offset, const void *source, unsigned length)
{
    (void)offset;
    (void)source;
    (void)length;
    __builtin_trap();
}

void copy_from_program(void *target, unsigned offset, unsigned length)
{
    (void)target;
    (void)offset;
    (void)length;
    __builtin_trap();
}

unsigned run_program(uint32_t directory, uint32_t entry, uint32_t stack)
{
    (void)directory;
    (void)entry;
    (void)stack;
    __builtin_trap();
}

void uefi_kernel_main(uint32_t kernel_base)
{
    x86_domains_configure(kernel_base, video->domain_arena, video->domain_bytes);
    x86_native_configure(video->domain_arena + video->domain_bytes);
    native_fb_configure(video->framebuffer, video->width, video->height,
                        video->stride, video->format);
    ps2_init();
    kernel_main();
}
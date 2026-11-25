#include <stdbool.h>
#include <stdint.h>

#include "uefi_handoff.h"
#include "x86_domains.h"
#include "font8x8_basic.h"

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

enum { KBD_DATA = 0x60, KBD_STATUS = 0x64 };

static bool write_keyboard(uint16_t port, uint8_t value)
{
    for (unsigned tries = 0; tries < 100000; ++tries) {
        if (!(inb(KBD_STATUS) & 2)) {
            outb(port, value);
            return true;
        }
    }
    return false;
}

static void setup_keyboard(void)
{
    if (!write_keyboard(KBD_STATUS, 0xad))
        return;
    for (unsigned i = 0; i < 32 && (inb(KBD_STATUS) & 1); ++i)
        inb(KBD_DATA);
    if (!write_keyboard(KBD_STATUS, 0x20))
        return;
    for (unsigned tries = 0; tries < 100000; ++tries) {
        if (inb(KBD_STATUS) & 1) {
            uint8_t config = inb(KBD_DATA);
            if (write_keyboard(KBD_STATUS, 0x60))
                write_keyboard(KBD_DATA, (config | 0x40) & ~1);
            break;
        }
    }
    if (write_keyboard(KBD_STATUS, 0xae))
        write_keyboard(KBD_DATA, 0xf4);
}

unsigned platform_getc(void)
{
    static const char plain[] =
        "\0\0331234567890-=\b\tqwertyuiop[]\r\0asdfghjkl;'"
        "\140\0\\zxcvbnm,./\0*\0 ";
    static const char shifted[] =
        "\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\r\0ASDFGHJKL:\""
        "~\0|ZXCVBNM<>?\0*\0 ";
    static uint8_t shift;
    static bool extended;

    for (;;) {
        uint8_t status = inb(KBD_STATUS);
        if (!(status & 1))
            continue;
        uint8_t scan = inb(KBD_DATA);
        if (status & 0x20)
            continue;
        if (scan == 0xe0) {
            extended = true;
        } else if (extended) {
            extended = false;
            if (scan == 0x1c)
                return '\r';
        } else if ((scan & 0x7f) == 0x2a || (scan & 0x7f) == 0x36) {
            uint8_t bit = (scan & 0x7f) == 0x2a ? 1 : 2;
            if (scan & 0x80)
                shift &= ~bit;
            else
                shift |= bit;
        } else if (scan < sizeof(plain) && plain[scan]) {
            return shift ? shifted[scan] : plain[scan];
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

unsigned run_program(uint32_t directory)
{
    (void)directory;
    __builtin_trap();
}

void uefi_kernel_main(uint32_t kernel_base)
{
    x86_domains_configure(kernel_base, video->domain_arena, video->domain_bytes);
    setup_keyboard();
    kernel_main();
}
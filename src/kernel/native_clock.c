#include "native.h"

enum { SPEAKER = 0x61, PIT_MODE = 0x43, PIT_CHANNEL2 = 0x42, TICKS_PER_MS = 1193 };

static uint16_t previous, remainder;
static uint32_t elapsed;

static void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0,%1" :: "a"(value), "Nd"(port));
}

static uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1,%0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint16_t counter(void)
{
    outb(PIT_MODE, 0x80);
    unsigned low = inb(PIT_CHANNEL2);
    return low | (unsigned)inb(PIT_CHANNEL2) << 8;
}

void native_clock_start(void)
{
    unsigned gate = inb(SPEAKER) & ~3u;
    outb(SPEAKER, gate);
    outb(PIT_MODE, 0xb4);
    outb(PIT_CHANNEL2, 0);
    outb(PIT_CHANNEL2, 0);
    outb(SPEAKER, gate | 1);
    previous = counter();
    remainder = 0;
    elapsed = 0;
}

uint32_t native_millis(void)
{
    uint16_t now = counter();
    unsigned ticks = (uint16_t)(previous - now) + remainder;
    previous = now;
    elapsed += ticks / TICKS_PER_MS;
    remainder = ticks % TICKS_PER_MS;
    return elapsed;
}

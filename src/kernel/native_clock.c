#include "native.h"
#include "platform.h"

enum { SPEAKER = 0x61, PIT_MODE = 0x43, PIT_CHANNEL2 = 0x42, TICKS_PER_MS = 1193 };

static uint16_t previous, remainder;
static uint8_t master_mask, slave_mask;
static bool timer_live, bios_clock;
static uint32_t elapsed, epoch;

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

static uint8_t cmos(unsigned reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

static unsigned rtc_number(unsigned value, bool binary)
{
    return binary ? value : (value >> 4) * 10 + (value & 15);
}

static bool leap(unsigned year)
{
    return !(year % 4) && ((year % 100) || !(year % 400));
}

static uint32_t rtc_epoch(void)
{
    for (unsigned attempt = 0; attempt < 10000; ++attempt) {
        if (cmos(0x0a) & 0x80)
            continue;
        unsigned seconds = cmos(0), minutes = cmos(2), hours = cmos(4);
        unsigned date = cmos(7), month = cmos(8), year = cmos(9), century = cmos(0x32);
        unsigned flags = cmos(0x0b);
        if ((cmos(0x0a) & 0x80) || seconds != cmos(0))
            continue;
        bool binary = flags & 4;
        bool pm = hours & 0x80;
        seconds = rtc_number(seconds, binary);
        minutes = rtc_number(minutes, binary);
        hours = rtc_number(hours & 0x7f, binary);
        if (!(flags & 2))
            hours = hours % 12 + (pm ? 12 : 0);
        date = rtc_number(date, binary);
        month = rtc_number(month, binary);
        year = rtc_number(year, binary);
        century = rtc_number(century, binary);
        year += (century == 19 || century == 20 ? century : year >= 70 ? 19 : 20) * 100;
        if (year < 1970 || month < 1 || month > 12 || hours > 23 || minutes > 59 || seconds > 59)
            return 0;
        const unsigned days_per_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (!date || date > days_per_month[month - 1] + (month == 2 && leap(year)))
            return 0;
        unsigned days = date - 1;
        for (unsigned i = 1970; i < year; ++i)
            days += 365 + leap(i);
        for (unsigned i = 1; i < month; ++i)
            days += days_per_month[i - 1] + (i == 2 && leap(year));
        return days * 86400 + hours * 3600 + minutes * 60 + seconds;
    }
    return 0;
}

uint32_t native_epoch(void)
{
    return epoch;
}

static void pic_setup(uint8_t master, uint8_t slave, uint8_t mask1, uint8_t mask2)
{
    outb(0x20, 0x11);
    outb(0xa0, 0x11);
    outb(0x21, master);
    outb(0xa1, slave);
    outb(0x21, 4);
    outb(0xa1, 2);
    outb(0x21, 1);
    outb(0xa1, 1);
    outb(0x21, mask1);
    outb(0xa1, mask2);
}

void native_clock_stop(void)
{
    if (!timer_live)
        return;
    timer_live = false;
    outb(PIT_MODE, 0x36);
    outb(0x40, 0);
    outb(0x40, 0);
    if (!bios_clock)
        pic_setup(0x08, 0x70, master_mask, slave_mask);
}

void native_clock_domain(bool entering)
{
    if (bios_clock)
        pic_setup(entering ? 0x20 : 0x08, entering ? 0x28 : 0x70,
                  entering ? 0xfe : master_mask, entering ? 0xff : slave_mask);
}

void native_clock_start(void)
{
    master_mask = inb(0x21);
    slave_mask = inb(0xa1);
    bios_clock = platform_can_run();
    if (!bios_clock)
        pic_setup(0x20, 0x28, 0xfe, 0xff);
    outb(PIT_MODE, 0x34);
    unsigned quantum = 10 * TICKS_PER_MS;
    outb(0x40, quantum);
    outb(0x40, quantum >> 8);
    timer_live = true;
    epoch = rtc_epoch();
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

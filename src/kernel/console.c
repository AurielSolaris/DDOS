#include "console.h"

#include "platform.h"

void print(const char *text)
{
    while (*text)
        platform_putc((uint8_t)*text++);
}

void number(uint32_t value)
{
    if (value >= 10)
        number(value / 10);
    platform_putc('0' + value % 10);
}

void print_bytes(const uint8_t *data, unsigned size)
{
    for (unsigned i = 0; i < size; ++i)
        platform_putc(data[i]);
}
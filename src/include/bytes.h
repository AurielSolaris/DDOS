#ifndef DDOS_BYTES_H
#define DDOS_BYTES_H

#include <stdint.h>

static inline uint16_t read16(const uint8_t *bytes)
{
    return bytes[0] | (uint16_t)bytes[1] << 8;
}

static inline uint32_t read32(const uint8_t *bytes)
{
    return read16(bytes) | (uint32_t)read16(bytes + 2) << 16;
}

static inline void write16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = value;
    bytes[1] = value >> 8;
}

static inline void write32(uint8_t *bytes, uint32_t value)
{
    write16(bytes, value);
    write16(bytes + 2, value >> 16);
}

#endif
#include "ata.h"

enum {
    COUNT = 2, LBA_LOW, LBA_MID, LBA_HIGH, DRIVE, STATUS,
    BUSY = 0x80, READY = 0x40, FAULT = 0x20, DRQ = 8, ERROR = 1,
};

static uint32_t capacities[2];
static unsigned selected;
static uint16_t io = 0x1f0, alt = 0x3f6;
static uint8_t device_head = 0xf0;

static void ata_select(unsigned device)
{
    selected = device;
    io = device ? 0x170 : 0x1f0;
    alt = device ? 0x376 : 0x3f6;
    device_head = device ? 0xe0 : 0xf0;
}

#define capacity capacities[selected]

static uint8_t in8(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint16_t in16(void)
{
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(io));
    return value;
}

static void out8(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" :: "a"(value), "Nd"(port));
}

static void out16(uint16_t value)
{
    __asm__ volatile ("outw %0, %1" :: "a"(value), "Nd"(io));
}

static void delay(void)
{
    for (unsigned i = 0; i < 4; ++i)
        in8(alt);
}

static bool wait_for(uint8_t set, uint8_t clear, bool check_errors)
{
    for (unsigned i = 0; i < 100000; ++i) {
        uint8_t status = in8(io + STATUS);
        if (status == 0 || status == 0xff)
            return false;
        if (status & BUSY)
            continue;
        if (check_errors && (status & (ERROR | FAULT)))
            return false;
        if ((status & set) == set && !(status & clear))
            return true;
    }
    return false;
}

static uint32_t ata_probe(void)
{
    capacity = 0;
    out8(io + DRIVE, device_head & ~0x40);
    delay();
    if (!wait_for(0, DRQ, false))
        return 0;
    out8(io + COUNT, 0);
    out8(io + LBA_LOW, 0);
    out8(io + LBA_MID, 0);
    out8(io + LBA_HIGH, 0);
    out8(io + STATUS, 0xec);
    delay();
    if (!wait_for(DRQ, 0, true) || in8(io + LBA_MID) || in8(io + LBA_HIGH))
        return 0;

    uint16_t capabilities = 0;
    uint32_t sectors = 0;
    for (unsigned i = 0; i < 256; ++i) {
        uint16_t word = in16();
        if (i == 49)
            capabilities = word;
        if (i == 60)
            sectors = word;
        if (i == 61)
            sectors |= (uint32_t)word << 16;
    }
    if (capabilities & (1u << 9))
        capacity = sectors > (1u << 28) ? 1u << 28 : sectors;
    return capacity;
}

static bool transfer(uint32_t lba, uint8_t command)
{
    if (lba >= capacity)
        return false;
    out8(io + DRIVE, device_head | (lba >> 24));
    delay();
    if (!wait_for(READY, DRQ, false))
        return false;
    out8(io + COUNT, 1);
    out8(io + LBA_LOW, lba);
    out8(io + LBA_MID, lba >> 8);
    out8(io + LBA_HIGH, lba >> 16);
    out8(io + STATUS, command);
    delay();
    return wait_for(DRQ, 0, true);
}

static bool ata_read(uint32_t lba, void *buffer)
{
    if (!transfer(lba, 0x20))
        return false;
    uint8_t *bytes = buffer;
    for (unsigned i = 0; i < 512; i += 2) {
        uint16_t word = in16();
        bytes[i] = word;
        bytes[i + 1] = word >> 8;
    }
    return wait_for(READY, DRQ, true);
}

static bool ata_write(uint32_t lba, const void *buffer)
{
    if (!transfer(lba, 0x30))
        return false;
    const uint8_t *bytes = buffer;
    for (unsigned i = 0; i < 512; i += 2)
        out16(bytes[i] | (uint16_t)bytes[i + 1] << 8);
    return wait_for(READY, DRQ, true);
}

static bool ata_flush(void)
{
    if (!capacity || !wait_for(READY, DRQ, true))
        return false;
    out8(io + STATUS, 0xe7);
    delay();
    return wait_for(READY, DRQ, true);
}
static bool read_sector(unsigned unit, uint32_t lba, void *buffer)
{
    ata_select(unit);
    return ata_read(lba, buffer);
}

static bool write_sector(unsigned unit, uint32_t lba, const void *buffer)
{
    ata_select(unit);
    return ata_write(lba, buffer);
}

static bool flush_device(unsigned unit)
{
    ata_select(unit);
    return ata_flush();
}

bool ata_block_device(unsigned unit, struct block_device *out)
{
    ata_select(unit);
    uint32_t sectors = ata_probe();
    *out = (struct block_device){sectors, unit, read_sector, write_sector, flush_device};
    return sectors != 0;
}

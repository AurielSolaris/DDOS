#ifndef DDOS_BLOCK_H
#define DDOS_BLOCK_H

#include <stdbool.h>
#include <stdint.h>

enum { BLOCK_SECTOR_BYTES = 512 };
struct block_device {
    uint32_t sectors;
    unsigned unit;
    bool (*read_sector)(unsigned, uint32_t, void *);
    bool (*write_sector)(unsigned, uint32_t, const void *);
    bool (*flush)(unsigned);
};

bool block_read(const struct block_device *, uint32_t, unsigned, void *);
bool block_write(const struct block_device *, uint32_t, unsigned, const void *);
bool block_flush(const struct block_device *);

#endif

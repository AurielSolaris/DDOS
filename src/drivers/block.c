#include "block.h"

bool block_read(const struct block_device *device, uint32_t lba, unsigned count, void *buffer)
{
    if (!device || !device->read_sector || lba > device->sectors || count > device->sectors - lba)
        return false;
    uint8_t *bytes = buffer;
    for (unsigned i = 0; i < count; ++i) {
        if (!device->read_sector(device->unit, lba + i, bytes + i * BLOCK_SECTOR_BYTES))
            return false;
    }
    return true;
}

bool block_write(const struct block_device *device, uint32_t lba, unsigned count, const void *buffer)
{
    if (!device || !device->write_sector || lba > device->sectors || count > device->sectors - lba)
        return false;
    const uint8_t *bytes = buffer;
    for (unsigned i = 0; i < count; ++i) {
        if (!device->write_sector(device->unit, lba + i, bytes + i * BLOCK_SECTOR_BYTES))
            return false;
    }
    return true;
}

bool block_flush(const struct block_device *device)
{
    return device && device->flush && device->flush(device->unit);
}

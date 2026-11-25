#ifndef DDOS_ATA_H
#define DDOS_ATA_H

#include <stdbool.h>
#include <stdint.h>

void ata_select(unsigned device);
uint32_t ata_probe(void);
bool ata_read(uint32_t lba, void *buffer);
bool ata_write(uint32_t lba, const void *buffer);
bool ata_flush(void);

#endif
#ifndef DDOS_ATA_H
#define DDOS_ATA_H

#include "block.h"

bool ata_block_device(unsigned unit, struct block_device *out);

#endif
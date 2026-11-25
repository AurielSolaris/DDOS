#ifndef DDOS_FAT12_H
#define DDOS_FAT12_H

#include <stdbool.h>
#include <stdint.h>

struct fat12_file {
    uint32_t size;
    unsigned cluster;
};

typedef void (*fat12_entry_fn)(const char *, uint32_t);
typedef void (*fat12_data_fn)(const uint8_t *, unsigned, void *);

bool fat12_mount(void);
bool fat12_list(fat12_entry_fn visit);
bool fat12_open(const char *name, bool com, struct fat12_file *file);
bool fat12_read(const struct fat12_file *file, fat12_data_fn visit, void *context);

#endif
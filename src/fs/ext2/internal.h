#ifndef DDOS_EXT2_INTERNAL_H
#define DDOS_EXT2_INTERNAL_H

#include "ext2.h"
#include "block.h"

enum {
    MAX_BLOCK = 4096,
    EXT2_MAGIC = 0xef53,
    EXT2_FILETYPE = 2,
    EXT2_VALID = 1,
    EXT2_INDEXED = 0x1000,
};

struct ext2_mount_state {
    const struct block_device *device;
    uint32_t block_size, blocks, inodes, first_block, blocks_per_group;
    uint32_t inodes_per_group, inode_size, groups;
    bool mounted, filetype, writable;
};

extern struct ext2_mount_state *ext2_active;
#define ext2_volume (*ext2_active)

bool ext2_read_block(uint32_t block, void *buffer);
bool ext2_write_block(uint32_t block, const void *buffer);
bool ext2_inode_location(uint32_t number, uint32_t *block, unsigned *offset);
bool ext2_map_block(const struct ext2_inode *inode, uint32_t logical, uint32_t *block);
bool ext2_dir_record(const uint8_t *entry, unsigned remaining, unsigned *record,
                     unsigned *length);

#endif
#ifndef DDOS_EXT2_H
#define DDOS_EXT2_H

#include <stdbool.h>
#include <stdint.h>

enum { EXT2_ROOT = 2, EXT2_DIRECTORY = 0x4000, EXT2_REGULAR = 0x8000, EXT2_WRITE_LIMIT = 8192 * 1024 };

struct ext2_inode {
    uint32_t number, size, flags, block[15];
    uint16_t kind, mode;
};

enum ext2_result {
    EXT2_OK, EXT2_MISSING, EXT2_INVALID, EXT2_IO,
    EXT2_EXISTS, EXT2_FULL, EXT2_READ_ONLY, EXT2_NOT_EMPTY, EXT2_WRONG_TYPE,
};

typedef void (*ext2_entry_fn)(const char *, unsigned, const struct ext2_inode *);
typedef void (*ext2_data_fn)(const uint8_t *, unsigned);

struct block_device;
void ext2_select(unsigned volume);
bool ext2_mount(const struct block_device *device);
bool ext2_mounted(void);
bool ext2_writable(void);
bool ext2_get_inode(uint32_t number, struct ext2_inode *out);
enum ext2_result ext2_lookup_dos(uint32_t start, const char *path, struct ext2_inode *out);
enum ext2_result ext2_read_chunk(const struct ext2_inode *file, uint32_t position,
                                 const uint8_t **data, unsigned *size);
enum ext2_result ext2_write_chunk(const struct ext2_inode *file, uint32_t position,
                                  const uint8_t *source, unsigned size);
enum ext2_result ext2_lookup(uint32_t start, const char *path, struct ext2_inode *out);
enum ext2_result ext2_next(const struct ext2_inode *, uint32_t *, char *,
                           struct ext2_inode *);
enum ext2_result ext2_remove(uint32_t, const char *, bool);
enum ext2_result ext2_rename(uint32_t, const char *, uint32_t, const char *);
enum ext2_result ext2_list(const struct ext2_inode *dir, ext2_entry_fn visit);
enum ext2_result ext2_read(const struct ext2_inode *file, ext2_data_fn visit);
enum ext2_result ext2_create(uint32_t cwd, const char *path, const uint8_t *contents,
                            unsigned size, bool directory);

#endif
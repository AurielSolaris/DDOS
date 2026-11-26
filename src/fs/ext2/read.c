#include "internal.h"

#include "bytes.h"

static struct ext2_mount_state volumes[3];
struct ext2_mount_state *ext2_active = &volumes[0];

void ext2_select(unsigned volume)
{
    ext2_active = &volumes[volume];
}

bool ext2_mounted(void)
{
    return ext2_volume.mounted;
}

static uint8_t metadata[MAX_BLOCK] __attribute__((section(".fs_workspace")));
static uint8_t data[MAX_BLOCK] __attribute__((section(".fs_workspace")));

bool ext2_read_block(uint32_t block, void *buffer)
{
    if (block >= ext2_volume.blocks)
        return false;
    unsigned sectors = ext2_volume.block_size / BLOCK_SECTOR_BYTES;
    return block_read(ext2_volume.device, block * sectors, sectors, buffer);
}

bool ext2_write_block(uint32_t block, const void *buffer)
{
    if (block >= ext2_volume.blocks)
        return false;
    unsigned sectors = ext2_volume.block_size / BLOCK_SECTOR_BYTES;
    return block_write(ext2_volume.device, block * sectors, sectors, buffer);
}

bool ext2_inode_location(uint32_t number, uint32_t *block, unsigned *offset)
{
    if (!number || number > ext2_volume.inodes)
        return false;
    uint32_t group = (number - 1) / ext2_volume.inodes_per_group;
    uint32_t local = (number - 1) % ext2_volume.inodes_per_group;
    uint32_t descriptor = ext2_volume.first_block + 1 +
                          group / (ext2_volume.block_size / 32);
    unsigned index = (group % (ext2_volume.block_size / 32)) * 32;
    if (group >= ext2_volume.groups || !ext2_read_block(descriptor, metadata))
        return false;
    uint32_t table = read32(metadata + index + 8);
    unsigned per_block = ext2_volume.block_size / ext2_volume.inode_size;
    uint32_t displacement = local / per_block;
    if (!table || table >= ext2_volume.blocks ||
        displacement >= ext2_volume.blocks - table)
        return false;
    *block = table + displacement;
    *offset = local % per_block * ext2_volume.inode_size;
    return true;
}

bool ext2_get_inode(uint32_t number, struct ext2_inode *inode)
{
    uint32_t block;
    unsigned offset;
    if (!ext2_inode_location(number, &block, &offset) ||
        !ext2_read_block(block, metadata))
        return false;
    const uint8_t *entry = metadata + offset;
    inode->number = number;
    inode->size = read32(entry + 4);
    inode->mode = read16(entry);
    inode->kind = inode->mode & 0xf000;
    inode->flags = read32(entry + 32);
    if (!read16(entry + 26) || (inode->kind == EXT2_REGULAR && read32(entry + 108)))
        return false;
    for (unsigned i = 0; i < 15; ++i)
        inode->block[i] = read32(entry + 40 + 4 * i);
    return true;
}

static bool writable_layout(const uint8_t *super)
{
    static const char label[16] = "DDOS_DATA";

    if (ext2_volume.block_size != 1024 || ext2_volume.groups != 1 ||
        ext2_volume.inode_size != 128 || read32(super + 84) != 11 ||
        read32(super + 92) || read32(super + 96) != EXT2_FILETYPE ||
        read32(super + 100) || read16(super + 58) != EXT2_VALID)
        return false;
    for (unsigned i = 0; i < sizeof(label); ++i) {
        if ((uint8_t)label[i] != super[120 + i])
            return false;
    }
    return true;
}

bool ext2_mount(const struct block_device *device)
{
    ext2_volume.mounted = ext2_volume.writable = false;
    ext2_volume.device = device;
    if (!block_read(device, 2, 2, metadata))
        return false;
    uint32_t log = read32(metadata + 24), revision = read32(metadata + 76);
    uint32_t compatible = read32(metadata + 92), incompatible = read32(metadata + 96);
    uint32_t read_only = read32(metadata + 100);
    if (read16(metadata + 56) != EXT2_MAGIC || log > 2 || revision > 1 ||
        (compatible & 4) || (incompatible & ~EXT2_FILETYPE) || (read_only & ~3u))
        return false;

    ext2_volume.block_size = 1024u << log;
    ext2_volume.first_block = read32(metadata + 20);
    ext2_volume.blocks = read32(metadata + 4);
    ext2_volume.inodes = read32(metadata);
    ext2_volume.blocks_per_group = read32(metadata + 32);
    ext2_volume.inodes_per_group = read32(metadata + 40);
    ext2_volume.inode_size = revision ? read16(metadata + 88) : 128;
    ext2_volume.filetype = (incompatible & EXT2_FILETYPE) != 0;
    if (ext2_volume.first_block != (log == 0) ||
        ext2_volume.blocks <= ext2_volume.first_block + 1 ||
        ext2_volume.blocks > device->sectors / (ext2_volume.block_size / BLOCK_SECTOR_BYTES) ||
        ext2_volume.inodes < EXT2_ROOT ||
        !ext2_volume.blocks_per_group || !ext2_volume.inodes_per_group ||
        ext2_volume.blocks_per_group > ext2_volume.block_size * 8 ||
        ext2_volume.inodes_per_group > ext2_volume.block_size * 8 ||
        ext2_volume.inode_size < 128 ||
        ext2_volume.block_size % ext2_volume.inode_size)
        return false;

    uint32_t groups = (ext2_volume.blocks - ext2_volume.first_block - 1) /
                      ext2_volume.blocks_per_group + 1;
    if ((ext2_volume.inodes - 1) / ext2_volume.inodes_per_group >= groups)
        return false;
    ext2_volume.groups = groups;
    bool writable = device->write_sector && device->flush && revision == 1 && writable_layout(metadata);
    struct ext2_inode root;
    if (!ext2_get_inode(EXT2_ROOT, &root) || root.kind != EXT2_DIRECTORY ||
        !root.size || root.size % ext2_volume.block_size)
        return false;
    ext2_volume.mounted = true;
    ext2_volume.writable = writable;
    return true;
}

bool ext2_writable(void)
{
    return ext2_volume.writable;
}

bool ext2_map_block(const struct ext2_inode *inode, uint32_t logical, uint32_t *block)
{
    unsigned fanout = ext2_volume.block_size / 4;
    if (logical < 12) {
        *block = inode->block[logical];
        return *block < ext2_volume.blocks;
    }
    logical -= 12;
    unsigned level = 1;
    uint32_t span = fanout;
    while (logical >= span && level < 3) {
        logical -= span;
        span *= fanout;
        ++level;
    }
    if (logical >= span)
        return false;
    *block = inode->block[11 + level];
    for (; level && *block; --level) {
        if (!ext2_read_block(*block, metadata))
            return false;
        span /= fanout;
        *block = read32(metadata + 4 * (logical / span));
        logical %= span;
    }
    return *block < ext2_volume.blocks;
}

bool ext2_dir_record(const uint8_t *entry, unsigned remaining, unsigned *record,
                     unsigned *length)
{
    if (remaining < 8)
        return false;
    *record = read16(entry + 4);
    *length = ext2_volume.filetype ? entry[6] : read16(entry + 6);
    return *record >= 8 && *record % 4 == 0 && *record <= remaining &&
           *length <= 255 && *length <= *record - 8;
}

static char dos_upper(char c)
{
    return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c;
}

static enum ext2_result walk(const struct ext2_inode *dir, const char *name,
                            unsigned length, struct ext2_inode *found,
                            ext2_entry_fn visit, bool fold)
{
    uint32_t folded_match = 0;
    bool ambiguous = false;
    unsigned block_size = ext2_volume.block_size;
    if (dir->kind != EXT2_DIRECTORY || dir->size % block_size)
        return EXT2_INVALID;
    for (uint32_t i = 0; i < dir->size / block_size; ++i) {
        uint32_t physical;
        if (!ext2_map_block(dir, i, &physical) || !physical ||
            !ext2_read_block(physical, data))
            return EXT2_IO;
        for (unsigned offset = 0; offset < block_size;) {
            const uint8_t *entry = data + offset;
            unsigned record, bytes;
            if (!ext2_dir_record(entry, block_size - offset, &record, &bytes))
                return EXT2_IO;
            offset += record;
            uint32_t number = read32(entry);
            if (!number)
                continue;
            const char *label = (const char *)(entry + 8);
            if (!name) {
                struct ext2_inode child;
                if (!ext2_get_inode(number, &child))
                    return EXT2_IO;
                visit(label, bytes, &child);
            } else if (bytes == length) {
                unsigned j = 0;
                while (j < length && label[j] == name[j])
                    ++j;
                if (j == length)
                    return ext2_get_inode(number, found) ? EXT2_OK : EXT2_IO;
                if (fold) {
                    j = 0;
                    while (j < length && dos_upper(label[j]) == dos_upper(name[j]))
                        ++j;
                    if (j == length) {
                        ambiguous = folded_match != 0;
                        folded_match = number;
                    }
                }
            }
        }
    }
    if (ambiguous)
        return EXT2_INVALID;
    if (folded_match)
        return ext2_get_inode(folded_match, found) ? EXT2_OK : EXT2_IO;
    return name ? EXT2_MISSING : EXT2_OK;
}

static bool separator(char c)
{
    return c == '/' || c == '\\';
}

static enum ext2_result lookup(uint32_t start, const char *path, struct ext2_inode *out,
                               bool fold)
{
    if (!ext2_volume.mounted)
        return EXT2_MISSING;
    if (!ext2_get_inode(separator(*path) ? EXT2_ROOT : start, out))
        return EXT2_IO;
    while (*path) {
        while (separator(*path))
            ++path;
        if (!*path)
            return out->kind == EXT2_DIRECTORY ? EXT2_OK : EXT2_INVALID;
        const char *name = path;
        while (*path && !separator(*path))
            ++path;
        unsigned length = (unsigned)(path - name);
        if (length > 255)
            return EXT2_INVALID;
        enum ext2_result result = walk(out, name, length, out, 0, fold);
        if (result != EXT2_OK)
            return result;
    }
    return EXT2_OK;
}

enum ext2_result ext2_lookup(uint32_t start, const char *path, struct ext2_inode *out)
{
    return lookup(start, path, out, false);
}

enum ext2_result ext2_lookup_dos(uint32_t start, const char *path, struct ext2_inode *out)
{
    return lookup(start, path, out, true);
}

enum ext2_result ext2_next(const struct ext2_inode *dir, uint32_t *position,
                           char *name, struct ext2_inode *out)
{
    unsigned block_size = ext2_volume.block_size;
    if (dir->kind != EXT2_DIRECTORY || dir->size % block_size)
        return EXT2_INVALID;
    while (*position < dir->size) {
        unsigned offset = *position % block_size;
        uint32_t physical;
        if (!ext2_map_block(dir, *position / block_size, &physical) ||
            !physical || !ext2_read_block(physical, data))
            return EXT2_IO;
        while (offset < block_size) {
            const uint8_t *entry = data + offset;
            unsigned record, length;
            if (!ext2_dir_record(entry, block_size - offset, &record, &length))
                return EXT2_IO;
            *position += record;
            offset += record;
            uint32_t number = read32(entry);
            if (!number)
                continue;
            for (unsigned i = 0; i < length; ++i)
                name[i] = entry[8 + i];
            name[length] = 0;
            return ext2_get_inode(number, out) ? EXT2_OK : EXT2_IO;
        }
    }
    return EXT2_MISSING;
}

enum ext2_result ext2_list(const struct ext2_inode *dir, ext2_entry_fn visit)
{
    return walk(dir, 0, 0, 0, visit, false);
}

enum ext2_result ext2_read_chunk(const struct ext2_inode *file, uint32_t position,
                                 const uint8_t **out, unsigned *size)
{
    if (file->kind != EXT2_REGULAR)
        return EXT2_INVALID;
    *out = data;
    if (position >= file->size) {
        *size = 0;
        return EXT2_OK;
    }
    unsigned offset = position % ext2_volume.block_size;
    unsigned count = ext2_volume.block_size - offset;
    if (count > file->size - position)
        count = file->size - position;
    uint32_t block;
    if (!ext2_map_block(file, position / ext2_volume.block_size, &block))
        return EXT2_IO;
    if (block) {
        if (!ext2_read_block(block, data))
            return EXT2_IO;
    } else {
        for (unsigned i = offset; i < offset + count; ++i)
            data[i] = 0;
    }
    *out = data + offset;
    *size = count;
    return EXT2_OK;
}

enum ext2_result ext2_read(const struct ext2_inode *file, ext2_data_fn visit)
{
    uint32_t position = 0;
    do {
        const uint8_t *chunk;
        unsigned size;
        enum ext2_result result = ext2_read_chunk(file, position, &chunk, &size);
        if (result != EXT2_OK)
            return result;
        if (!size)
            break;
        visit(chunk, size);
        position += size;
    } while (position < file->size);
    return EXT2_OK;
}

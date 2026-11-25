#include "internal.h"

#include "ata.h"
#include "bytes.h"

enum { BLOCK = 1024, SUPER = 1, GROUP = 2, INODE_SIZE = 128 };

static uint8_t super[BLOCK], group[BLOCK], block_bits[BLOCK], inode_bits[BLOCK];
static uint8_t contents[BLOCK];
static unsigned bitmap_block, bitmap_inode, first_data;

static unsigned record_size(unsigned name)
{
    return (8 + name + 3) & ~3u;
}

static void dirent(uint8_t *entry, uint32_t inode, unsigned size,
                   const char *name, unsigned length, bool directory)
{
    write32(entry, inode);
    write16(entry + 4, size);
    entry[6] = length;
    entry[7] = directory ? 2 : 1;
    for (unsigned i = 0; i < length; ++i)
        entry[8 + i] = name[i];
}

static bool used(const uint8_t *bitmap, unsigned index)
{
    return (bitmap[index / 8] & (1u << (index % 8))) != 0;
}

static unsigned allocate(uint8_t *bitmap, unsigned first, unsigned count)
{
    for (unsigned i = first; i < count; ++i) {
        if (!used(bitmap, i)) {
            bitmap[i / 8] |= 1u << (i % 8);
            return i + 1;
        }
    }
    return 0;
}

static bool separator(char c)
{
    return c == '/' || c == '\\';
}

struct dir_slot {
    unsigned offset, old_size, used_size;
    uint32_t block;
};

static enum ext2_result find_slot(const struct ext2_inode *parent,
                                  unsigned length, unsigned first_data,
                                  struct dir_slot *slot)
{
    if (parent->kind != EXT2_DIRECTORY || parent->size % BLOCK)
        return EXT2_INVALID;
    if (parent->flags & EXT2_INDEXED)
        return EXT2_READ_ONLY;
    for (uint32_t logical = 0; logical < parent->size / BLOCK; ++logical) {
        uint32_t block;
        if (!ext2_map_block(parent, logical, &block) || block < first_data ||
            !used(block_bits, block - 1) || !ext2_read_block(block, contents))
            return EXT2_IO;
        for (unsigned offset = 0; offset < BLOCK;) {
            unsigned size, name;
            uint8_t *entry = contents + offset;
            if (!ext2_dir_record(entry, BLOCK - offset, &size, &name))
                return EXT2_IO;
            unsigned occupied = read32(entry) ? record_size(name) : 0;
            if (size - occupied >= record_size(length)) {
                slot->offset = offset;
                slot->old_size = size;
                slot->used_size = occupied;
                slot->block = block;
                return EXT2_OK;
            }
            offset += size;
        }
    }
    return EXT2_FULL;
}

static bool store_inode(uint32_t number, uint32_t block, unsigned size, bool directory)
{
    uint32_t table;
    unsigned offset;
    if (!ext2_inode_location(number, &table, &offset) || !ext2_read_block(table, contents))
        return false;
    uint8_t *inode = contents + offset;
    for (unsigned i = 0; i < INODE_SIZE; ++i)
        inode[i] = 0;
    write16(inode, directory ? 0x41ed : 0x81a4);
    write32(inode + 4, size);
    write16(inode + 26, directory ? 2 : 1);
    write32(inode + 28, block ? 2 : 0);
    write32(inode + 40, block);
    return ext2_write_block(table, contents);
}

static bool add_parent_link(uint32_t number)
{
    uint32_t table;
    unsigned offset;
    if (!ext2_inode_location(number, &table, &offset) || !ext2_read_block(table, contents))
        return false;
    uint8_t *links = contents + offset + 26;
    unsigned count = read16(links);
    if (count == 0xffff)
        return false;
    write16(links, count + 1);
    return ext2_write_block(table, contents);
}

static bool load_allocation(void)
{
    if (!ext2_read_block(SUPER, super) || !ext2_read_block(GROUP, group))
        return false;
    bitmap_block = read32(group);
    bitmap_inode = read32(group + 4);
    unsigned table = read32(group + 8);
    first_data = table + (ext2_volume.inodes + 7) / 8;
    return bitmap_block == 3 && bitmap_inode == 4 && table == 5 &&
           first_data < ext2_volume.blocks &&
           ext2_volume.inodes == ext2_volume.inodes_per_group &&
           ext2_read_block(bitmap_block, block_bits) &&
           ext2_read_block(bitmap_inode, inode_bits);
}

static bool begin_write(void)
{
    /* Recovery tools can detect an interrupted metadata update. */
    write16(super + 58, 0);
    ext2_volume.writable = false;
    return ext2_write_block(SUPER, super) && ata_flush();
}

static bool finish_write(void)
{
    if (!ata_flush())
        return false;
    write16(super + 58, EXT2_VALID);
    ext2_volume.writable = ext2_write_block(SUPER, super) && ata_flush();
    return ext2_volume.writable;
}

enum ext2_result ext2_create(uint32_t cwd, const char *path, const uint8_t *source,
                            unsigned size, bool directory)
{
    if (!ext2_writable())
        return EXT2_READ_ONLY;
    if (size > BLOCK || !*path)
        return EXT2_INVALID;

    const char *end = path;
    while (*end)
        ++end;
    while (end > path && separator(end[-1]) && directory)
        --end;
    const char *name = end;
    while (name > path && !separator(name[-1]))
        --name;
    unsigned prefix = name - path, length = end - name;
    char part[256];
    if (!length || length > 255 || prefix > 255)
        return EXT2_INVALID;

    for (unsigned i = 0; i < prefix; ++i)
        part[i] = path[i];
    part[prefix] = '\0';
    struct ext2_inode parent;
    enum ext2_result result = ext2_lookup(cwd, part, &parent);
    if (result != EXT2_OK)
        return result;
    for (unsigned i = 0; i < length; ++i)
        part[i] = name[i];
    part[length] = '\0';
    struct ext2_inode existing;
    result = ext2_lookup(parent.number, part, &existing);
    if (result != EXT2_MISSING)
        return result == EXT2_OK ? EXT2_EXISTS : result;

    if (!load_allocation())
        return EXT2_IO;

    struct dir_slot slot;
    result = find_slot(&parent, length, first_data, &slot);
    if (result != EXT2_OK)
        return result;
    unsigned inode = allocate(inode_bits, 10, ext2_volume.inodes);
    bool needs_block = directory || size;
    unsigned block = needs_block ? allocate(block_bits, first_data - 1,
                                             ext2_volume.blocks - 1) : 0;
    if (!inode || (needs_block && !block) || !read32(super + 16) ||
        !read16(group + 14) || (needs_block && (!read32(super + 12) || !read16(group + 12))))
        return EXT2_FULL;

    if (!begin_write())
        return EXT2_IO;

    if (needs_block) {
        for (unsigned i = 0; i < BLOCK; ++i)
            contents[i] = 0;
        if (directory) {
            dirent(contents, inode, 12, ".", 1, true);
            dirent(contents + 12, parent.number, BLOCK - 12, "..", 2, true);
            size = BLOCK;
        } else {
            for (unsigned i = 0; i < size; ++i)
                contents[i] = source[i];
        }
        if (!ext2_write_block(block, contents) || !ext2_write_block(bitmap_block, block_bits))
            return EXT2_IO;
        write32(super + 12, read32(super + 12) - 1);
        write16(group + 12, read16(group + 12) - 1);
    }

    if (!store_inode(inode, block, size, directory) || !ext2_write_block(bitmap_inode, inode_bits))
        return EXT2_IO;
    write32(super + 16, read32(super + 16) - 1);
    write16(group + 14, read16(group + 14) - 1);
    if (directory) {
        if (!add_parent_link(parent.number))
            return EXT2_IO;
        write16(group + 16, read16(group + 16) + 1);
    }

    if (!ext2_write_block(GROUP, group) || !ext2_write_block(SUPER, super) ||
        !ext2_read_block(slot.block, contents))
        return EXT2_IO;
    uint8_t *entry = contents + slot.offset;
    if (slot.used_size)
        write16(entry + 4, slot.used_size);
    dirent(entry + slot.used_size, inode, slot.old_size - slot.used_size,
           name, length, directory);
    if (!ext2_write_block(slot.block, contents))
        return EXT2_IO;
    return finish_write() ? EXT2_OK : EXT2_IO;
}

enum ext2_result ext2_write_chunk(const struct ext2_inode *file, uint32_t position,
                                  const uint8_t *source, unsigned size)
{
    if (!ext2_writable())
        return EXT2_READ_ONLY;
    if (file->kind != EXT2_REGULAR || !(file->mode & 0222) || file->flags ||
        file->size > EXT2_WRITE_LIMIT || file->block[12] || file->block[13] ||
        file->block[14])
        return EXT2_INVALID;
    if (position > EXT2_WRITE_LIMIT || size > EXT2_WRITE_LIMIT - position)
        return EXT2_FULL;
    if (size > BLOCK - position % BLOCK)
        return EXT2_INVALID;
    if (!load_allocation())
        return EXT2_IO;

    uint8_t inode_contents[BLOCK];
    uint32_t table;
    unsigned offset;
    if (!ext2_inode_location(file->number, &table, &offset) ||
        !ext2_read_block(table, inode_contents))
        return EXT2_IO;
    uint8_t *inode = inode_contents + offset;
    if (read32(inode + 104))
        return EXT2_INVALID;

    unsigned allocated = 0;
    for (unsigned i = 0; i < 12; ++i) {
        uint32_t block = file->block[i];
        if (!block)
            continue;
        if (block < first_data || block >= ext2_volume.blocks ||
            !used(block_bits, block - 1))
            return EXT2_IO;
        for (unsigned j = 0; j < i; ++j) {
            if (block == file->block[j])
                return EXT2_IO;
        }
        ++allocated;
    }

    uint32_t new_size = size && position + size < file->size ? file->size : position + size;
    unsigned index = position / BLOCK;
    uint32_t changed_block = 0;
    int free_delta = 0;

    if (size) {
        changed_block = file->block[index];
        if (changed_block) {
            if (!ext2_read_block(changed_block, contents))
                return EXT2_IO;
        } else {
            if (!read32(super + 12) || !read16(group + 12))
                return EXT2_FULL;
            changed_block = allocate(block_bits, first_data - 1, ext2_volume.blocks - 1);
            if (!changed_block)
                return EXT2_FULL;
            for (unsigned i = 0; i < BLOCK; ++i)
                contents[i] = 0;
            write32(inode + 40 + index * 4, changed_block);
            ++allocated;
            --free_delta;
        }
        for (unsigned i = 0; i < size; ++i)
            contents[position % BLOCK + i] = source[i];
    } else if (new_size < file->size) {
        if (position % BLOCK && file->block[index]) {
            changed_block = file->block[index];
            if (!ext2_read_block(changed_block, contents))
                return EXT2_IO;
            for (unsigned i = position % BLOCK; i < BLOCK; ++i)
                contents[i] = 0;
        }
        for (unsigned i = (position + BLOCK - 1) / BLOCK; i < 12; ++i) {
            uint32_t block = file->block[i];
            if (!block)
                continue;
            unsigned bit = block - 1;
            block_bits[bit / 8] &= ~(1u << (bit % 8));
            write32(inode + 40 + i * 4, 0);
            --allocated;
            ++free_delta;
        }
    }

    write32(inode + 4, new_size);
    write32(inode + 28, allocated * 2);
    if (!begin_write())
        return EXT2_IO;
    if (changed_block && !ext2_write_block(changed_block, contents))
        return EXT2_IO;
    if (free_delta < 0 && !ext2_write_block(bitmap_block, block_bits))
        return EXT2_IO;
    if (!ext2_write_block(table, inode_contents))
        return EXT2_IO;
    if (free_delta > 0 && !ext2_write_block(bitmap_block, block_bits))
        return EXT2_IO;
    if (free_delta) {
        write32(super + 12, read32(super + 12) + free_delta);
        write16(group + 12, read16(group + 12) + free_delta);
        if (!ext2_write_block(GROUP, group))
            return EXT2_IO;
    }
    return finish_write() ? EXT2_OK : EXT2_IO;
}

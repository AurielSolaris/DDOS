#include "internal.h"

#include "bytes.h"

enum { BLOCK = 1024, SUPER = 1, GROUP = 2, INODE_SIZE = 128,
       DIRECT = 12, FANOUT = BLOCK / 4 };

#define EXT2_WORKSPACE __attribute__((section(".fs_workspace")))
static uint8_t super[BLOCK] EXT2_WORKSPACE;
static uint8_t group[BLOCK] EXT2_WORKSPACE;
static uint8_t block_bits[BLOCK] EXT2_WORKSPACE;
static uint8_t inode_bits[BLOCK] EXT2_WORKSPACE;
static uint8_t contents[BLOCK] EXT2_WORKSPACE;
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
    bool fresh;
};

static uint32_t claim_block(int *delta);
static bool commit_counts(int delta);
static void clear_block(uint8_t *buffer);

static enum ext2_result find_slot(const struct ext2_inode *parent,
                                  unsigned length, struct dir_slot *slot, int *delta)
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
                slot->fresh = false;
                return EXT2_OK;
            }
            offset += size;
        }
    }
    if (parent->size / BLOCK >= DIRECT)
        return EXT2_FULL;
    uint32_t block = claim_block(delta);
    if (!block)
        return EXT2_FULL;
    *slot = (struct dir_slot){0, BLOCK, 0, block, true};
    return EXT2_OK;
}

static bool publish_entry(const struct ext2_inode *parent, const struct dir_slot *slot,
                          uint32_t number, const char *name, unsigned length, bool directory)
{
    if (slot->fresh) {
        uint32_t table;
        unsigned offset;
        if (!ext2_inode_location(parent->number, &table, &offset) || !ext2_read_block(table, contents))
            return false;
        uint8_t *inode = contents + offset;
        write32(inode + 40 + parent->size / BLOCK * 4, slot->block);
        write32(inode + 4, parent->size + BLOCK);
        write32(inode + 28, read32(inode + 28) + 2);
        if (!ext2_write_block(table, contents))
            return false;
        clear_block(contents);
    } else if (!ext2_read_block(slot->block, contents)) {
        return false;
    }
    uint8_t *entry = contents + slot->offset;
    if (slot->used_size)
        write16(entry + 4, slot->used_size);
    dirent(entry + slot->used_size, number, slot->old_size - slot->used_size, name, length, directory);
    return ext2_write_block(slot->block, contents);
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

static bool adjust_links(uint32_t number, int delta)
{
    uint32_t table;
    unsigned offset;
    if (!ext2_inode_location(number, &table, &offset) || !ext2_read_block(table, contents))
        return false;
    uint8_t *links = contents + offset + 26;
    unsigned count = read16(links);
    if ((int)count + delta < 1 || (int)count + delta > 0xffff)
        return false;
    write16(links, count + delta);
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
    return ext2_write_block(SUPER, super) && block_flush(ext2_volume.device);
}

static bool finish_write(void)
{
    if (!block_flush(ext2_volume.device))
        return false;
    write16(super + 58, EXT2_VALID);
    ext2_volume.writable = ext2_write_block(SUPER, super) && block_flush(ext2_volume.device);
    return ext2_volume.writable;
}

static enum ext2_result split_path(uint32_t cwd, const char *path, bool trailing,
                                   struct ext2_inode *parent, char name[256], unsigned *length)
{

    const char *end = path;
    while (*end)
        ++end;
    while (end > path && separator(end[-1]) && trailing)
        --end;
    const char *start = end;
    while (start > path && !separator(start[-1]))
        --start;
    unsigned prefix = start - path, bytes = end - start;
    if (!bytes || bytes > 255 || prefix > 255 ||
        (bytes == 1 && start[0] == '.') ||
        (bytes == 2 && start[0] == '.' && start[1] == '.'))
        return EXT2_INVALID;
    for (unsigned i = 0; i < prefix; ++i)
        name[i] = path[i];
    name[prefix] = 0;
    enum ext2_result result = ext2_lookup(cwd, name, parent);
    if (result != EXT2_OK)
        return result;
    if (parent->kind != EXT2_DIRECTORY)
        return EXT2_INVALID;
    if (parent->flags & EXT2_INDEXED)
        return EXT2_READ_ONLY;
    for (unsigned i = 0; i < bytes; ++i)
        name[i] = start[i];
    name[bytes] = 0;
    *length = bytes;
    return EXT2_OK;
}

enum ext2_result ext2_create(uint32_t cwd, const char *path, const uint8_t *source,
                            unsigned size, bool directory)
{
    if (!ext2_writable())
        return EXT2_READ_ONLY;
    if (size > BLOCK || !*path)
        return EXT2_INVALID;
    struct ext2_inode parent, existing;
    char name[256];
    unsigned length;
    enum ext2_result result = split_path(cwd, path, directory, &parent, name, &length);
    if (result != EXT2_OK)
        return result;
    result = ext2_lookup(parent.number, name, &existing);
    if (result != EXT2_MISSING)
        return result == EXT2_OK ? EXT2_EXISTS : result;

    if (!load_allocation())
        return EXT2_IO;

    struct dir_slot slot;
    int delta = 0;
    result = find_slot(&parent, length, &slot, &delta);
    if (result != EXT2_OK)
        return result;
    unsigned inode = allocate(inode_bits, 10, ext2_volume.inodes);
    bool needs_block = directory || size;
    unsigned block = needs_block ? claim_block(&delta) : 0;
    if (!inode || (needs_block && !block) || !read32(super + 16) || !read16(group + 14))
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
        if (!ext2_write_block(block, contents))
            return EXT2_IO;
    }

    if (!store_inode(inode, block, size, directory) || !ext2_write_block(bitmap_inode, inode_bits))
        return EXT2_IO;
    write32(super + 16, read32(super + 16) - 1);
    write16(group + 14, read16(group + 14) - 1);
    if (directory) {
        if (!adjust_links(parent.number, 1))
            return EXT2_IO;
        write16(group + 16, read16(group + 16) + 1);
    }

    if (!(delta ? commit_counts(delta) : ext2_write_block(GROUP, group)) ||
        !publish_entry(&parent, &slot, inode, name, length, directory))
        return EXT2_IO;
    return finish_write() ? EXT2_OK : EXT2_IO;
}



static uint8_t pointers[2][BLOCK] EXT2_WORKSPACE;

static bool allocated_block(uint32_t block)
{
    return block >= first_data && block < ext2_volume.blocks && used(block_bits, block - 1);
}

static uint32_t claim_block(int *delta)
{
    if ((int)read16(group + 12) + *delta <= 0 || (int)read32(super + 12) + *delta <= 0)
        return 0;
    uint32_t block = allocate(block_bits, first_data - 1, ext2_volume.blocks - 1);
    if (block)
        --*delta;
    return block;
}

static bool free_block(uint32_t block, int *delta)
{
    if (!allocated_block(block))
        return false;
    unsigned bit = block - 1;
    block_bits[bit / 8] &= ~(1u << (bit % 8));
    ++*delta;
    return true;
}

static bool commit_counts(int delta)
{
    if (!delta)
        return true;
    write32(super + 12, read32(super + 12) + delta);
    write16(group + 12, read16(group + 12) + delta);
    return ext2_write_block(bitmap_block, block_bits) && ext2_write_block(GROUP, group);
}

static void clear_block(uint8_t *buffer)
{
    for (unsigned i = 0; i < BLOCK; ++i)
        buffer[i] = 0;
}

/* Release all descendants at or beyond the first logical block to discard. */
static bool prune(uint8_t *link, unsigned level, unsigned first, unsigned keep, int *delta)
{
    uint32_t block = read32(link);
    unsigned span = level == 2 ? FANOUT * FANOUT : level == 1 ? FANOUT : 1;
    if (!block || first + span <= keep)
        return true;
    if (!allocated_block(block))
        return false;
    if (level) {
        uint8_t *table = pointers[level - 1];
        if (!ext2_read_block(block, table))
            return false;
        bool live = false;
        for (unsigned i = 0; i < FANOUT; ++i) {
            uint8_t *child = table + i * 4;
            if (!prune(child, level - 1, first + i * (span / FANOUT), keep, delta))
                return false;
            live |= read32(child) != 0;
        }
        if (live)
            return ext2_write_block(block, table);
    }
    write32(link, 0);
    return free_block(block, delta);
}

enum ext2_result ext2_write_chunk(const struct ext2_inode *file, uint32_t position,
                                  const uint8_t *source, unsigned size)
{
    if (!ext2_writable())
        return EXT2_READ_ONLY;
    if (file->kind != EXT2_REGULAR || !(file->mode & 0222) || file->flags ||
        file->size > EXT2_WRITE_LIMIT || file->block[14])
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

    uint32_t new_size = position + size;
    unsigned index = position / BLOCK;
    uint32_t changed_block = 0;
    uint32_t tables[2];
    unsigned levels = 0;
    int delta = 0;
    if (size) {
        if (new_size < file->size)
            new_size = file->size;
        uint8_t *link;
        if (index >= DIRECT) {
            index -= DIRECT;
            unsigned level = index < FANOUT ? 1 : 2;
            if (level == 2)
                index -= FANOUT;
            link = inode + 40 + (DIRECT + level - 1) * 4;
            for (; level; --level) {
                uint8_t *entries = pointers[levels];
                uint32_t block = read32(link);
                if (block) {
                    if (!allocated_block(block) || !ext2_read_block(block, entries))
                        return EXT2_IO;
                } else {
                    block = claim_block(&delta);
                    if (!block)
                        return EXT2_FULL;
                    clear_block(entries);
                    write32(link, block);
                }
                tables[levels++] = block;
                unsigned slot = level == 2 ? index / FANOUT : index % FANOUT;
                link = entries + slot * 4;
            }
        } else {
            link = inode + 40 + index * 4;
        }
        changed_block = read32(link);
        if (changed_block) {
            if (!allocated_block(changed_block) || !ext2_read_block(changed_block, contents))
                return EXT2_IO;
        } else {
            changed_block = claim_block(&delta);
            if (!changed_block)
                return EXT2_FULL;
            clear_block(contents);
            write32(link, changed_block);
        }
        for (unsigned i = 0; i < size; ++i)
            contents[position % BLOCK + i] = source[i];
    } else if (new_size < file->size && position % BLOCK) {
        if (!ext2_map_block(file, index, &changed_block))
            return EXT2_IO;
        if (changed_block) {
            if (!allocated_block(changed_block) || !ext2_read_block(changed_block, contents))
                return EXT2_IO;
            for (unsigned i = position % BLOCK; i < BLOCK; ++i)
                contents[i] = 0;
        }
    }

    if (!begin_write())
        return EXT2_IO;
    if (changed_block && !ext2_write_block(changed_block, contents))
        return EXT2_IO;
    while (levels) {
        --levels;
        if (!ext2_write_block(tables[levels], pointers[levels]))
            return EXT2_IO;
    }
    if (!size && new_size < file->size) {
        unsigned keep = (new_size + BLOCK - 1) / BLOCK;
        for (unsigned i = 0; i < DIRECT; ++i) {
            if (!prune(inode + 40 + i * 4, 0, i, keep, &delta))
                return EXT2_IO;
        }
        if (!prune(inode + 88, 1, DIRECT, keep, &delta) ||
            !prune(inode + 92, 2, DIRECT + FANOUT, keep, &delta))
            return EXT2_IO;
    }
    uint32_t sectors = read32(inode + 28);
    if (delta > 0 && sectors < (unsigned)delta * 2)
        return EXT2_IO;
    write32(inode + 4, new_size);
    write32(inode + 28, sectors - delta * 2);
    if ((delta < 0 && !commit_counts(delta)) ||
        !ext2_write_block(table, inode_contents) || (delta > 0 && !commit_counts(delta)))
        return EXT2_IO;
    return finish_write() ? EXT2_OK : EXT2_IO;
}

static enum ext2_result locate_entry(const struct ext2_inode *parent, const char *name,
                                      unsigned length, uint32_t *block, unsigned *offset)
{
    if (parent->size % BLOCK)
        return EXT2_IO;
    for (unsigned i = 0; i < parent->size / BLOCK; ++i) {
        if (!ext2_map_block(parent, i, block) || !allocated_block(*block) ||
            !ext2_read_block(*block, contents))
            return EXT2_IO;
        for (unsigned at = 0; at < BLOCK;) {
            const uint8_t *entry = contents + at;
            unsigned bytes, record;
            if (!ext2_dir_record(entry, BLOCK - at, &record, &bytes))
                return EXT2_IO;
            if (read32(entry) && bytes == length) {
                unsigned j = 0;
                while (j < length && entry[8 + j] == (uint8_t)name[j])
                    ++j;
                if (j == length) {
                    *offset = at;
                    return EXT2_OK;
                }
            }
            at += record;
        }
    }
    return EXT2_MISSING;
}

enum ext2_result ext2_remove(uint32_t cwd, const char *path, bool directory)
{
    if (!ext2_writable())
        return EXT2_READ_ONLY;
    struct ext2_inode parent, item;
    char name[256];
    unsigned length;
    enum ext2_result result = split_path(cwd, path, directory, &parent, name, &length);
    if (result != EXT2_OK)
        return result;
    result = ext2_lookup(parent.number, name, &item);
    if (result != EXT2_OK)
        return result;
    if (item.kind != (directory ? EXT2_DIRECTORY : EXT2_REGULAR))
        return EXT2_WRONG_TYPE;
    if (item.number <= 10 || item.flags || item.block[14])
        return EXT2_INVALID;
    if (directory) {
        uint32_t position = 0;
        struct ext2_inode child;
        char label[256];
        while ((result = ext2_next(&item, &position, label, &child)) == EXT2_OK) {
            if (!(label[0] == '.' && (!label[1] || (label[1] == '.' && !label[2]))))
                return EXT2_NOT_EMPTY;
        }
        if (result != EXT2_MISSING)
            return result;
    }
    if (!load_allocation() || !used(inode_bits, item.number - 1))
        return EXT2_IO;
    uint32_t table, entry_block;
    unsigned offset, entry_offset;
    uint8_t inode_contents[BLOCK];
    if (!ext2_inode_location(item.number, &table, &offset) ||
        !ext2_read_block(table, inode_contents))
        return EXT2_IO;
    uint8_t *inode = inode_contents + offset;
    if (read32(inode + 104))
        return EXT2_INVALID;
    result = locate_entry(&parent, name, length, &entry_block, &entry_offset);
    if (result != EXT2_OK)
        return result;
    unsigned links = read16(inode + 26);
    if (!links || (directory && links != 2))
        return EXT2_IO;
    if (!begin_write())
        return EXT2_IO;
    write32(contents + entry_offset, 0);
    if (!ext2_write_block(entry_block, contents))
        return EXT2_IO;
    if (!directory && links > 1) {
        write16(inode + 26, links - 1);
        return ext2_write_block(table, inode_contents) && finish_write() ? EXT2_OK : EXT2_IO;
    }
    int delta = 0;
    for (unsigned i = 0; i < DIRECT; ++i) {
        if (!prune(inode + 40 + i * 4, 0, i, 0, &delta))
            return EXT2_IO;
    }
    if (!prune(inode + 88, 1, DIRECT, 0, &delta) ||
        !prune(inode + 92, 2, DIRECT + FANOUT, 0, &delta))
        return EXT2_IO;
    for (unsigned i = 0; i < INODE_SIZE; ++i)
        inode[i] = 0;
    unsigned bit = item.number - 1;
    inode_bits[bit / 8] &= ~(1u << (bit % 8));
    write32(super + 16, read32(super + 16) + 1);
    write16(group + 14, read16(group + 14) + 1);
    if (directory)
        write16(group + 16, read16(group + 16) - 1);
    if (!ext2_write_block(table, inode_contents) ||
        !ext2_write_block(bitmap_inode, inode_bits) ||
        !(delta ? commit_counts(delta) : ext2_write_block(GROUP, group)))
        return EXT2_IO;
    if (directory && !adjust_links(parent.number, -1))
        return EXT2_IO;
    return finish_write() ? EXT2_OK : EXT2_IO;
}

enum ext2_result ext2_rename(uint32_t cwd, const char *old, uint32_t new_cwd, const char *path)
{
    if (!ext2_writable())
        return EXT2_READ_ONLY;
    struct ext2_inode from, to, item, existing;
    char old_name[256], name[256];
    unsigned old_length, length;
    enum ext2_result result = split_path(cwd, old, true, &from, old_name, &old_length);
    if (result != EXT2_OK)
        return result;
    result = ext2_lookup(from.number, old_name, &item);
    if (result != EXT2_OK)
        return result;
    bool directory = item.kind == EXT2_DIRECTORY;
    if ((!directory && item.kind != EXT2_REGULAR) || item.flags || item.number <= 10)
        return EXT2_INVALID;
    result = split_path(new_cwd, path, directory, &to, name, &length);
    if (result != EXT2_OK)
        return result;
    result = ext2_lookup(to.number, name, &existing);
    if (result == EXT2_OK)
        return existing.number == item.number ? EXT2_OK : EXT2_EXISTS;
    if (result != EXT2_MISSING)
        return result;
    bool change_parent = directory && from.number != to.number;
    if (change_parent) {
        struct ext2_inode ancestor = to;
        unsigned budget = ext2_volume.inodes;
        while (ancestor.number != EXT2_ROOT) {
            if (ancestor.number == item.number)
                return EXT2_INVALID;
            if (!budget-- || ext2_lookup(ancestor.number, "..", &ancestor) != EXT2_OK)
                return EXT2_IO;
        }
    }
    if (!load_allocation())
        return EXT2_IO;
    uint32_t old_block, parent_block = 0;
    unsigned old_offset, parent_offset = 0;
    if (change_parent && locate_entry(&item, "..", 2, &parent_block, &parent_offset) != EXT2_OK)
        return EXT2_IO;
    result = locate_entry(&from, old_name, old_length, &old_block, &old_offset);
    if (result != EXT2_OK)
        return result;
    if (from.number == to.number && record_size(length) <= read16(contents + old_offset + 4)) {
        unsigned record = read16(contents + old_offset + 4);
        if (!begin_write())
            return EXT2_IO;
        dirent(contents + old_offset, item.number, record, name, length, directory);
        return ext2_write_block(old_block, contents) && finish_write() ? EXT2_OK : EXT2_IO;
    }
    struct dir_slot slot;
    int delta = 0;
    result = find_slot(&to, length, &slot, &delta);
    if (result != EXT2_OK)
        return result;
    if (!begin_write())
        return EXT2_IO;
    if (!commit_counts(delta) || !publish_entry(&to, &slot, item.number, name, length, directory) ||
        !ext2_read_block(old_block, contents))
        return EXT2_IO;
    write32(contents + old_offset, 0);
    if (!ext2_write_block(old_block, contents))
        return EXT2_IO;
    if (change_parent) {
        if (!ext2_read_block(parent_block, contents))
            return EXT2_IO;
        write32(contents + parent_offset, to.number);
        if (!ext2_write_block(parent_block, contents) || !adjust_links(from.number, -1) ||
            !adjust_links(to.number, 1))
            return EXT2_IO;
    }
    return finish_write() ? EXT2_OK : EXT2_IO;
}

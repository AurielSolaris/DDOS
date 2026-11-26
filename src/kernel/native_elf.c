#include "native.h"

#include "bytes.h"
#include "ext2.h"

enum {
    PT_LOAD = 1, PT_DYNAMIC = 2, PT_INTERP = 3, PF_X = 1, PF_W = 2,
    ELF_HEADER = 52, PROGRAM_HEADER = 32,
};

static bool read_at(const struct ext2_inode *file, uint32_t offset,
                    uint8_t *output, unsigned length)
{
    while (length) {
        const uint8_t *chunk;
        unsigned size;
        if (ext2_read_chunk(file, offset, &chunk, &size) != EXT2_OK || !size)
            return false;
        if (size > length)
            size = length;
        for (unsigned i = 0; i < size; ++i)
            output[i] = chunk[i];
        output += size;
        offset += size;
        length -= size;
    }
    return true;
}

bool native_load_elf(struct native_space *space, const struct ext2_inode *file,
                     struct native_elf *elf)
{
    uint8_t header[ELF_HEADER];
    if (file->kind != EXT2_REGULAR || file->size < sizeof(header) ||
        !read_at(file, 0, header, sizeof(header)) ||
        read32(header) != 0x464c457f || header[4] != 1 || header[5] != 1 || header[6] != 1 ||
        read16(header + 16) != 2 || read16(header + 18) != 3 ||
        read32(header + 20) != 1 || read16(header + 40) != ELF_HEADER ||
        read16(header + 42) != PROGRAM_HEADER)
        return false;

    uint32_t off = read32(header + 28), entry = read32(header + 24);
    unsigned count = read16(header + 44);
    unsigned table_size = count * PROGRAM_HEADER;
    if (!count || count > 64 || off > file->size || table_size > file->size - off)
        return false;
    uint8_t ph[PROGRAM_HEADER];
    uint32_t first = UINT32_MAX, last = 0, phdr = 0;
    bool executable = false;
    for (unsigned i = 0; i < count; ++i) {
        if (!read_at(file, off + i * PROGRAM_HEADER, ph, sizeof(ph)))
            return false;
        uint32_t type = read32(ph);
        if (type == PT_DYNAMIC || type == PT_INTERP)
            return false;
        if (type != PT_LOAD)
            continue;
        uint32_t position = read32(ph + 4), address = read32(ph + 8);
        uint32_t size = read32(ph + 16), memory = read32(ph + 20);
        uint32_t flags = read32(ph + 24), align = read32(ph + 28);
        if (position > file->size || size > file->size - position || size > memory ||
            memory > UINT32_MAX - address ||
            (align > 1 && ((align & (align - 1)) || ((address ^ position) & (align - 1)))))
            return false;
        if (!memory)
            continue;
        if (address < first)
            first = address;
        if (address + memory > last)
            last = address + memory;
        if ((flags & PF_X) && entry >= address && entry - address < size)
            executable = true;
        if (off >= position && off - position <= size && table_size <= size - (off - position))
            phdr = address + off - position;
    }
    if (!executable || first == UINT32_MAX)
        return false;
    uint32_t base = first & ~(NATIVE_PAGE - 1);
    bool legacy = !base && last <= 2 * NATIVE_PAGE;
    unsigned capacity = legacy ? 8 * NATIVE_PAGE : space->capacity;
    unsigned stack = legacy ? NATIVE_PAGE : 16 * NATIVE_PAGE;
    /* Keep the translated segment base above the kernel bridge mappings. */
    if (base > 0x40000000u - capacity || last - base > capacity - stack - 3 * NATIVE_PAGE)
        return false;
    uint32_t heap_start = (last + NATIVE_PAGE - 1) & ~(NATIVE_PAGE - 1);
    if (legacy)
        heap_start = 2 * NATIVE_PAGE;

    uint8_t permissions[NATIVE_MAX_PAGES];
    for (unsigned i = 0; i < NATIVE_MAX_PAGES; ++i)
        permissions[i] = 0;
    for (unsigned i = 0; i < capacity; ++i)
        space->stage[i] = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (!read_at(file, off + i * PROGRAM_HEADER, ph, sizeof(ph)))
            return false;
        if (read32(ph) != PT_LOAD || !read32(ph + 20))
            continue;
        uint32_t position = read32(ph + 4), address = read32(ph + 8) - base;
        uint32_t size = read32(ph + 16), memory = read32(ph + 20);
        if (address >= capacity || memory > capacity - address || size > memory ||
            position > file->size || size > file->size - position ||
            !read_at(file, position, space->stage + address, size))
            return false;
        unsigned mode = (read32(ph + 24) & PF_W) ? 3 : 1;
        for (unsigned page = address / NATIVE_PAGE; page <= (address + memory - 1) / NATIVE_PAGE; ++page)
            permissions[page] |= mode;
    }
    for (unsigned page = (capacity - stack) / NATIVE_PAGE; page < capacity / NATIVE_PAGE; ++page)
        permissions[page] = 3;

    native_commit(space, base, capacity);
    for (unsigned page = 0; page < NATIVE_MAX_PAGES; ++page)
        native_page(space, page, permissions[page]);
    elf->entry = entry;
    elf->phdr = phdr;
    elf->phnum = phdr ? count : 0;
    elf->heap_start = heap_start;
    elf->stack_bottom = base + capacity - stack;
    return true;
}

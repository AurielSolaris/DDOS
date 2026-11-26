#include "dos.h"

#include "bytes.h"
#include "console.h"
#include "ext2.h"
#include "vfs.h"
#include "platform.h"

enum { PROGRAM_LIMIT = 0xff00 - 0x100, LOAD_OFFSET = 0x100, LOAD_SEGMENT = 0x2010 };

static uint8_t psp[256];

static void load_psp(const char *arguments)
{
    psp[0] = 0xcd;
    psp[1] = 0x20;
    psp[3] = 0x30;  /* First segment beyond the 64 KiB program area. */
    unsigned length = 0;
    if (*arguments)
        psp[0x81 + length++] = ' ';
    while (*arguments && length < 126)
        psp[0x81 + length++] = *arguments++;
    psp[0x80] = length;
    psp[0x81 + length] = '\r';
    copy_to_program(0, psp, sizeof(psp));
}

static bool program_path(const char *name, char path[134], bool *mz)
{
    unsigned i = 0;
    bool dot = false;
    if (name[0] != '/' && name[0] != '\\') {
        const char prefix[] = "/bin/";
        for (unsigned index = 0; prefix[index]; ++index)
            path[i++] = prefix[index];
    }
    for (; *name; ++name) {
        if (i == 129)
            return false;
        if (*name == '/' || *name == '\\')
            dot = false;
        else if (*name == '.')
            dot = true;
        path[i++] = *name;
    }
    if (!dot) {
        path[i++] = '.';
        path[i++] = 'C';
        path[i++] = 'O';
        path[i++] = 'M';
    } else {
        if (i < 4 || path[i - 4] != '.')
            return false;
        unsigned type = ((uint8_t)path[i - 3] | 32u) << 16 |
                        ((uint8_t)path[i - 2] | 32u) << 8 | ((uint8_t)path[i - 1] | 32u);
        if (type != 0x636f6d && type != 0x657865)
            return false;
        *mz = type == 0x657865;
    }
    path[i] = 0;
    return true;
}

static bool read_program(const struct ext2_inode *file, unsigned offset, unsigned length)
{
    for (unsigned done = 0; done < length;) {
        const uint8_t *chunk;
        unsigned size;
        if (ext2_read_chunk(file, offset + done, &chunk, &size) != EXT2_OK || !size)
            return false;
        if (size > length - done)
            size = length - done;
        copy_to_program(LOAD_OFFSET + done, chunk, size);
        done += size;
    }
    return true;
}

static bool mz_load(const struct ext2_inode *file, uint32_t *entry, uint32_t *stack)
{
    const uint8_t *header;
    unsigned size;
    if (ext2_read_chunk(file, 0, &header, &size) != EXT2_OK || size < 28 ||
        read16(header) != 0x5a4d)
        return false;
    unsigned last = read16(header + 2), pages = read16(header + 4);
    unsigned relocations = read16(header + 6), header_size = read16(header + 8) * 16u;
    unsigned minimum = read16(header + 10) * 16u, reloc_offset = read16(header + 24);
    unsigned ss = read16(header + 14), sp = read16(header + 16);
    unsigned ip = read16(header + 20), cs = read16(header + 22);
    if (!pages || last > 511 || header_size < 28 || read16(header + 26) != 0 ||
        (relocations && (reloc_offset < 28 || reloc_offset + relocations * 4u > header_size)))
        return false;
    unsigned end = (pages - 1) * 512u + (last ? last : 512);
    if (end <= header_size || end > file->size)
        return false;
    unsigned bytes = end - header_size;
    unsigned required = ((bytes + 15) & ~15u) + minimum;
    if (required > PROGRAM_LIMIT || cs * 16u + ip >= bytes || sp < 6 ||
        ss * 16u + sp > PROGRAM_LIMIT || !read_program(file, header_size, bytes))
        return false;

    const uint8_t zero[32] = {0};
    for (unsigned off = bytes; off < required;) {
        unsigned len = required - off < sizeof(zero) ? required - off : sizeof(zero);
        copy_to_program(LOAD_OFFSET + off, zero, len);
        off += len;
    }
    for (unsigned i = 0; i < relocations; ++i) {
        uint8_t rel[4];
        for (unsigned done = 0; done < sizeof(rel);) {
            const uint8_t *chunk;
            if (ext2_read_chunk(file, reloc_offset + i * 4u + done, &chunk, &size) != EXT2_OK || !size)
                return false;
            while (size-- && done < sizeof(rel))
                rel[done++] = *chunk++;
        }
        unsigned offset = read16(rel) + read16(rel + 2) * 16u;
        if (offset + 2 > bytes)
            return false;
        uint8_t word[2];
        copy_from_program(word, LOAD_OFFSET + offset, sizeof(word));
        write16(word, read16(word) + LOAD_SEGMENT);
        copy_to_program(LOAD_OFFSET + offset, word, sizeof(word));
    }
    *entry = ((LOAD_SEGMENT + cs) << 16) | ip;
    *stack = ((LOAD_SEGMENT + ss) << 16) | sp;
    return true;
}

bool dos_run(const char *name, const char *arguments, uint32_t directory)
{
    char path[134];
    struct ext2_inode file;
    bool mz = false;
    if (!vfs_select(VFS_B) || !program_path(name, path, &mz) ||
        ext2_lookup_dos(EXT2_ROOT, path, &file) != EXT2_OK || file.kind != EXT2_REGULAR)
        return false;
    if (!platform_can_run()) {
        print("DOS execution requires a BIOS boot\r\n");
        return true;
    }
    uint32_t entry = 0x20000100, stack = 0x2000fffc;
    if (mz ? !mz_load(&file, &entry, &stack) :
             (!file.size || file.size > PROGRAM_LIMIT || !read_program(&file, 0, file.size))) {
        print("Invalid or unreadable DOS executable\r\n");
        return true;
    }
    if (!mz) {
        const uint8_t zero[2] = {0};
        copy_to_program(0xfffc, zero, sizeof(zero));
    }
    load_psp(arguments);
    unsigned result = run_program(directory, entry, stack);
    if (result) {
        print("Program returned ");
        number(result);
        print("\r\n");
    }
    return true;
}
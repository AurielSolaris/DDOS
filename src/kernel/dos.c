#include "dos.h"

#include "console.h"
#include "ext2.h"
#include "vfs.h"
#include "platform.h"

enum { PROGRAM_LIMIT = 0xff00 - 0x100 };

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

static bool program_path(const char *name, char path[134])
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
    } else if (i < 4 || path[i - 4] != '.' || (path[i - 3] | 32) != 'c' ||
               (path[i - 2] | 32) != 'o' || (path[i - 1] | 32) != 'm') {
        return false;
    }
    path[i] = 0;
    return true;
}

bool dos_run(const char *name, const char *arguments, uint32_t directory)
{
    char path[134];
    struct ext2_inode file;
    if (!vfs_select(VFS_B) || !program_path(name, path) ||
        ext2_lookup_dos(EXT2_ROOT, path, &file) != EXT2_OK || file.kind != EXT2_REGULAR)
        return false;
    if (!platform_can_run()) {
        print("COM execution requires a BIOS boot\r\n");
        return true;
    }
    if (!file.size || file.size > PROGRAM_LIMIT) {
        print("Invalid COM program size\r\n");
        return true;
    }
    for (unsigned offset = 0; offset < file.size;) {
        const uint8_t *chunk;
        unsigned size;
        if (ext2_read_chunk(&file, offset, &chunk, &size) != EXT2_OK || !size) {
            print("File read error\r\n");
            return true;
        }
        copy_to_program(0x100 + offset, chunk, size);
        offset += size;
    }
    load_psp(arguments);
    unsigned result = run_program(directory);
    if (result) {
        print("Program returned ");
        number(result);
        print("\r\n");
    }
    return true;
}
#include "console.h"
#include "ext2.h"
#include "platform.h"
#include "vfs.h"

enum {
    FIRST_HANDLE = 5, LAST_HANDLE = 19, HANDLE_COUNT = LAST_HANDLE - FIRST_HANDLE + 1,
    DOS_BAD_FUNCTION = 1, DOS_FILE_NOT_FOUND = 2, DOS_PATH_NOT_FOUND = 3,
    DOS_TOO_MANY_FILES = 4, DOS_ACCESS_DENIED = 5, DOS_BAD_HANDLE = 6,
    DOS_BAD_DRIVE = 15, DOS_SEEK_ERROR = 25, DOS_IO_ERROR = 31,
    DOS_FILE_EXISTS = 80, DOS_DISK_FULL = 112,
    PROGRAM_BASE = 0x30000, PROGRAM_SIZE = 0x10000,
    DOS_PATH_SIZE = 256, WRITE_BLOCK = 1024, CARRY = 1,
};

/* Word layout produced by the real-mode interrupt gate in platform.asm. */
struct dos_frame {
    uint16_t di, si, bp, saved_sp, bx, dx, cx, ax;
    uint16_t es, ds, fs, gs, ip, cs, flags;
};

_Static_assert(sizeof(struct dos_frame) == 30, "DOS interrupt frame layout");

struct open_file {
    uint32_t inode, position;
    uint8_t mode, references;
};

static uint8_t handles[HANDLE_COUNT], transfer[WRITE_BLOCK];
static struct open_file files[HANDLE_COUNT];
static uint32_t current_directory;

void dos_files_begin(uint32_t directory)
{
    vfs_select(VFS_D);
    current_directory = directory;
    for (unsigned i = 0; i < HANDLE_COUNT; ++i) {
        handles[i] = 0;
        files[i].references = 0;
    }
}

static unsigned dos_error(enum ext2_result result, unsigned missing)
{
    switch (result) {
    case EXT2_OK: return 0;
    case EXT2_MISSING: return missing;
    case EXT2_EXISTS: return DOS_FILE_EXISTS;
    case EXT2_FULL: return DOS_DISK_FULL;
    case EXT2_IO: return DOS_IO_ERROR;
    default: return DOS_ACCESS_DENIED;
    }
}

static bool user_span(uint16_t segment, uint16_t offset, unsigned count, unsigned *address)
{
    uint32_t physical = (uint32_t)segment * 16 + offset;
    if (physical < PROGRAM_BASE || physical >= PROGRAM_BASE + PROGRAM_SIZE ||
        count > PROGRAM_SIZE - offset || count > PROGRAM_BASE + PROGRAM_SIZE - physical)
        return false;
    *address = physical - PROGRAM_BASE;
    return true;
}

static unsigned user_path(const struct dos_frame *frame, char name[DOS_PATH_SIZE],
                          const char **path)
{
    unsigned end = DOS_PATH_SIZE;
    if (end > PROGRAM_SIZE - frame->dx)
        end = PROGRAM_SIZE - frame->dx;
    unsigned address;
    if (!user_span(frame->ds, frame->dx, 1, &address))
        return DOS_ACCESS_DENIED;
    if (end > PROGRAM_SIZE - address)
        end = PROGRAM_SIZE - address;
    copy_from_program(name, address, end);
    unsigned i = 0;
    while (i < end && name[i])
        ++i;
    if (!i || i == end)
        return DOS_PATH_NOT_FOUND;
    *path = name;
    if (name[1] == ':') {
        if (name[0] != 'D' && name[0] != 'd')
            return DOS_BAD_DRIVE;
        *path += 2;
    }
    return 0;
}

static bool separator(char c)
{
    return c == '/' || c == '\\';
}

static unsigned parent_path(const char *path, struct ext2_inode *parent, const char **leaf)
{
    char prefix[DOS_PATH_SIZE];
    unsigned length = 0, split = 0;
    while (path[length]) {
        char c = path[length];
        if (separator(c))
            split = length + 1;
        else if (c == '*' || c == '?' || c == ':')
            return DOS_PATH_NOT_FOUND;
        ++length;
    }
    if (split == length)
        return DOS_PATH_NOT_FOUND;
    for (unsigned i = 0; i < split; ++i)
        prefix[i] = path[i];
    prefix[split] = '\0';
    *leaf = path + split;
    enum ext2_result result = ext2_lookup_dos(current_directory, prefix, parent);
    if (result == EXT2_OK && parent->kind != EXT2_DIRECTORY)
        return DOS_PATH_NOT_FOUND;
    return dos_error(result, DOS_PATH_NOT_FOUND);
}

static unsigned unused_handle(void)
{
    for (unsigned i = 0; i < HANDLE_COUNT; ++i) {
        if (!handles[i])
            return i + FIRST_HANDLE;
    }
    return 0;
}

static struct open_file *file_handle(unsigned handle)
{
    if (handle < FIRST_HANDLE || handle > LAST_HANDLE || !handles[handle - FIRST_HANDLE])
        return 0;
    return &files[handles[handle - FIRST_HANDLE] - 1];
}

static unsigned close_file(unsigned handle)
{
    struct open_file *file = file_handle(handle);
    if (!file)
        return DOS_BAD_HANDLE;
    --file->references;
    handles[handle - FIRST_HANDLE] = 0;
    return 0;
}

static unsigned open_file(const char *path, unsigned function, struct dos_frame *frame)
{
    bool create = function != 0x3d;
    unsigned mode = create ? 2 : frame->ax & 7;
    if (mode > 2 || (create && (frame->cx & ~0x20)))
        return DOS_ACCESS_DENIED;
    unsigned handle = unused_handle();
    if (!handle)
        return DOS_TOO_MANY_FILES;

    struct ext2_inode parent, inode;
    const char *leaf;
    unsigned error = parent_path(path, &parent, &leaf);
    if (error)
        return error;

    enum ext2_result result = ext2_lookup_dos(parent.number, leaf, &inode);
    if (result == EXT2_OK) {
        if (function == 0x5b)
            return DOS_FILE_EXISTS;
        if (inode.kind != EXT2_REGULAR ||
            (mode != 0 && (!ext2_writable() || !(inode.mode & 0222))))
            return DOS_ACCESS_DENIED;
        if (create) {
            result = ext2_write_chunk(&inode, 0, 0, 0);
            if (result != EXT2_OK)
                return dos_error(result, DOS_FILE_NOT_FOUND);
        }
    } else {
        if (result != EXT2_MISSING || !create)
            return dos_error(result, DOS_FILE_NOT_FOUND);
        result = ext2_create(parent.number, leaf, 0, 0, false);
        if (result != EXT2_OK)
            return dos_error(result, DOS_PATH_NOT_FOUND);
        if (ext2_lookup(parent.number, leaf, &inode) != EXT2_OK)
            return DOS_IO_ERROR;
    }

    unsigned index = 0;
    while (files[index].references)
        ++index;
    files[index].inode = inode.number;
    files[index].position = 0;
    files[index].mode = mode;
    files[index].references = 1;
    handles[handle - FIRST_HANDLE] = index + 1;
    frame->ax = handle;
    return 0;
}

static unsigned read_file(struct dos_frame *frame)
{
    struct open_file *file = file_handle(frame->bx);
    if (!file)
        return DOS_BAD_HANDLE;
    if (file->mode == 1)
        return DOS_ACCESS_DENIED;
    struct ext2_inode inode;
    if (!ext2_get_inode(file->inode, &inode))
        return DOS_IO_ERROR;
    unsigned done = 0, address;
    if (frame->cx && !user_span(frame->ds, frame->dx, frame->cx, &address))
        return DOS_ACCESS_DENIED;
    while (done < frame->cx) {
        const uint8_t *data;
        unsigned size;
        enum ext2_result result = ext2_read_chunk(&inode, file->position, &data, &size);
        if (result != EXT2_OK) {
            if (done)
                break;
            return dos_error(result, DOS_FILE_NOT_FOUND);
        }
        if (!size)
            break;
        if (size > frame->cx - done)
            size = frame->cx - done;
        copy_to_program(address + done, data, size);
        file->position += size;
        done += size;
    }
    frame->ax = done;
    return 0;
}

static unsigned write_file(struct dos_frame *frame)
{
    bool console = frame->bx == 1 || frame->bx == 2;
    struct open_file *file = file_handle(frame->bx);
    if (!file && !console)
        return DOS_BAD_HANDLE;
    if (file && file->mode == 0)
        return DOS_ACCESS_DENIED;

    unsigned address;
    if (frame->cx && !user_span(frame->ds, frame->dx, frame->cx, &address))
        return DOS_ACCESS_DENIED;
    if (console) {
        unsigned done = 0;
        while (done < frame->cx) {
            unsigned size = frame->cx - done;
            if (size > sizeof(transfer))
                size = sizeof(transfer);
            copy_from_program(transfer, address + done, size);
            print_bytes(transfer, size);
            done += size;
        }
        frame->ax = frame->cx;
        return 0;
    }

    if (!frame->cx) {
        struct ext2_inode inode;
        if (!ext2_get_inode(file->inode, &inode))
            return DOS_IO_ERROR;
        frame->ax = 0;
        return dos_error(ext2_write_chunk(&inode, file->position, 0, 0), DOS_FILE_NOT_FOUND);
    }

    unsigned done = 0;
    while (done < frame->cx) {
        struct ext2_inode inode;
        if (!ext2_get_inode(file->inode, &inode))
            return DOS_IO_ERROR;
        unsigned size = WRITE_BLOCK - file->position % WRITE_BLOCK;
        if (size > frame->cx - done)
            size = frame->cx - done;
        copy_from_program(transfer, address + done, size);
        enum ext2_result result = ext2_write_chunk(&inode, file->position, transfer, size);
        if (result != EXT2_OK) {
            if (done || result == EXT2_FULL)
                break; /* Return the bytes already committed. */
            return dos_error(result, DOS_FILE_NOT_FOUND);
        }
        file->position += size;
        done += size;
    }
    frame->ax = done;
    return 0;
}

static unsigned seek_file(struct dos_frame *frame)
{
    struct open_file *file = file_handle(frame->bx);
    if (!file)
        return DOS_BAD_HANDLE;
    uint32_t origin = 0;
    switch (frame->ax & 0xff) {
    case 0: break;
    case 1: origin = file->position; break;
    case 2: {
        struct ext2_inode inode;
        if (!ext2_get_inode(file->inode, &inode))
            return DOS_IO_ERROR;
        origin = inode.size;
        break;
    }
    default: return DOS_BAD_FUNCTION;
    }
    uint32_t delta = (uint32_t)frame->cx << 16 | frame->dx;
    uint32_t position = origin + delta;
    if ((delta & 0x80000000) ? position > origin : position < origin)
        return DOS_SEEK_ERROR;
    file->position = position;
    frame->ax = position;
    frame->dx = position >> 16;
    return 0;
}

static unsigned duplicate(struct dos_frame *frame, bool force)
{
    struct open_file *file = file_handle(frame->bx);
    if (!file || (force && (frame->cx < FIRST_HANDLE || frame->cx > LAST_HANDLE)))
        return DOS_BAD_HANDLE;
    unsigned handle = force ? frame->cx : unused_handle();
    if (!handle)
        return DOS_TOO_MANY_FILES;
    if (handle != frame->bx) {
        close_file(handle);
        handles[handle - FIRST_HANDLE] = handles[frame->bx - FIRST_HANDLE];
        ++file->references;
    }
    if (!force)
        frame->ax = handle;
    return 0;
}

static unsigned path_call(struct dos_frame *frame, unsigned function)
{
    char name[DOS_PATH_SIZE];
    const char *path;
    unsigned error = user_path(frame, name, &path);
    if (error)
        return error;
    if (function == 0x3c || function == 0x3d || function == 0x5b)
        return open_file(path, function, frame);

    struct ext2_inode inode;
    if (function == 0x39) {
        struct ext2_inode parent;
        const char *leaf;
        error = parent_path(path, &parent, &leaf);
        if (error)
            return error;
        enum ext2_result result = ext2_lookup_dos(parent.number, leaf, &inode);
        if (result != EXT2_MISSING)
            return result == EXT2_OK ? DOS_ACCESS_DENIED : dos_error(result, DOS_PATH_NOT_FOUND);
        return dos_error(ext2_create(parent.number, leaf, 0, 0, true), DOS_PATH_NOT_FOUND);
    }
    if (function == 0x43 && (frame->ax & 0xff))
        return DOS_BAD_FUNCTION;
    enum ext2_result result = ext2_lookup_dos(current_directory, path, &inode);
    if (result != EXT2_OK)
        return dos_error(result, function == 0x3b ? DOS_PATH_NOT_FOUND : DOS_FILE_NOT_FOUND);
    if (function == 0x3b) {
        if (inode.kind != EXT2_DIRECTORY)
            return DOS_PATH_NOT_FOUND;
        current_directory = inode.number;
    } else {
        if (inode.kind != EXT2_DIRECTORY && inode.kind != EXT2_REGULAR)
            return DOS_ACCESS_DENIED;
        frame->cx = (inode.kind == EXT2_DIRECTORY ? 0x10 : 0x20) |
                    ((inode.mode & 0222) ? 0 : 1);
    }
    return 0;
}

void dos_file_interrupt(struct dos_frame *frame)
{
    unsigned function = frame->ax >> 8;
    unsigned error;
    switch (function) {
    case 0x39: case 0x3b: case 0x3c: case 0x3d: case 0x43: case 0x5b:
        error = path_call(frame, function); break;
    case 0x3e: error = close_file(frame->bx); break;
    case 0x3f: error = read_file(frame); break;
    case 0x40: error = write_file(frame); break;
    case 0x42: error = seek_file(frame); break;
    case 0x45: case 0x46: error = duplicate(frame, function == 0x46); break;
    case 0x68: error = file_handle(frame->bx) ? 0 : DOS_BAD_HANDLE; break;
    default: error = DOS_BAD_FUNCTION; break;
    }
    if (error) {
        frame->ax = error;
        frame->flags |= CARRY;
    } else {
        frame->flags &= ~CARRY;
    }
}
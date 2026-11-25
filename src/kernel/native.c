#include "native.h"

#include "bytes.h"
#include "console.h"
#include "ext2.h"
#include "vfs.h"
#include "platform.h"

enum {
    ENOENT = 2, EIO = 5, ENOEXEC = 8, EBADF = 9, ENOMEM = 12, EACCES = 13, EFAULT = 14,
    EEXIST = 17, ENODEV = 19, ENOTDIR = 20, EISDIR = 21, EINVAL = 22, EMFILE = 24,
    ENOSPC = 28, ESPIPE = 29, EROFS = 30, ENAMETOOLONG = 36, ENOSYS = 38,
    AT_FDCWD = -100,
    HEAP_START = 2 * NATIVE_PAGE, HEAP_END = 5 * NATIVE_PAGE,
    MMAP_END = 7 * NATIVE_PAGE,
    O_CREAT = 0x40, O_EXCL = 0x80, O_TRUNC = 0x200, O_APPEND = 0x400,
};

struct file {
    uint32_t inode, position, flags;
    enum volume volume;
};

struct process {
    struct native_space space;
    uint32_t cwd, brk, tid_address;
    enum volume cwd_volume;
    char tmp_name[32];
    uint8_t tmp_data[256];
    unsigned tmp_size;
    unsigned standard;
    bool tls_set;
    struct file files[8];
};

static void copy(uint8_t *to, const uint8_t *from, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i)
        to[i] = from[i];
}

static bool load_elf(struct native_space *space, const struct ext2_inode *file)
{
    if (file->kind != EXT2_REGULAR || file->size < 52 || file->size > NATIVE_FILE_LIMIT)
        return false;
    for (unsigned off = 0; off < file->size;) {
        const uint8_t *chunk;
        unsigned size;
        if (ext2_read_chunk(file, off, &chunk, &size) != EXT2_OK || !size)
            return false;
        copy(space->file + off, chunk, size);
        off += size;
    }
    const uint8_t *elf = space->file;
    if (read32(elf) != 0x464c457f || elf[4] != 1 || elf[5] != 1 || elf[6] != 1 ||
        read16(elf + 16) != 2 || read16(elf + 18) != 3 ||
        read32(elf + 20) != 1 || read16(elf + 42) != 32)
        return false;
    uint32_t phoff = read32(elf + 28), entry = read32(elf + 24);
    unsigned count = read16(elf + 44), pages = 0;
    if (phoff > file->size || count > (file->size - phoff) / 32)
        return false;
    bool executable = false;
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *ph = elf + phoff + i * 32;
        uint32_t type = read32(ph);
        if (type == 2 || type == 3) /* PT_DYNAMIC or PT_INTERP */
            return false;
        if (type != 1)
            continue;
        uint32_t off = read32(ph + 4), address = read32(ph + 8);
        uint32_t bytes = read32(ph + 16), memory = read32(ph + 20);
        uint32_t flags = read32(ph + 24);
        if ((address != 0 && address != NATIVE_PAGE) || !memory ||
            memory > NATIVE_PAGE || bytes > memory || off > file->size ||
            bytes > file->size - off || (address == 0 ? flags != 5 : flags != 6))
            return false;
        unsigned mask = 1u << (address / NATIVE_PAGE);
        if (pages & mask)
            return false;
        pages |= mask;
        if (!address && entry < bytes)
            executable = true;
    }
    if (!executable)
        return false;
    for (unsigned i = 0; i < NATIVE_PAGES * NATIVE_PAGE; ++i)
        space->image[i] = 0;
    for (unsigned i = 2; i < NATIVE_PAGES - 1; ++i)
        native_page(space, i, false);
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *ph = elf + phoff + i * 32;
        if (read32(ph) == 1)
            copy(space->image + read32(ph + 8), elf + read32(ph + 4), read32(ph + 16));
    }
    uint32_t cs = space->frame.cs, ss = space->frame.ss;
    for (unsigned i = 0; i < sizeof(space->frame); ++i)
        ((volatile uint8_t *)&space->frame)[i] = 0;
    space->frame.cs = cs;
    space->frame.ss = ss;
    space->frame.eip = entry;
    space->frame.esp = NATIVE_PAGES * NATIVE_PAGE - 16;
    return true;
}

static uint8_t *user(struct process *p, uint32_t address, uint32_t size, bool writable)
{
    if (!size)
        return p->space.image;
    if (!address || address >= NATIVE_PAGES * NATIVE_PAGE ||
        size > NATIVE_PAGES * NATIVE_PAGE - address)
        return 0;
    for (unsigned i = address / NATIVE_PAGE; i <= (address + size - 1) / NATIVE_PAGE; ++i) {
        uint32_t flags = p->space.pages[i];
        if (!(flags & 1) || (writable && !(flags & 2)))
            return 0;
    }
    return p->space.image + address;
}

static int fs_error(enum ext2_result result)
{
    switch (result) {
    case EXT2_MISSING: return -ENOENT;
    case EXT2_EXISTS: return -EEXIST;
    case EXT2_FULL: return -ENOSPC;
    case EXT2_READ_ONLY: return -EROFS;
    case EXT2_INVALID: return -EINVAL;
    default: return -EIO;
    }
}

static struct file *fd(struct process *p, uint32_t number)
{
    if (number < 3 || number >= 3 + sizeof(p->files) / sizeof(p->files[0]) ||
        !p->files[number - 3].inode)
        return 0;
    return &p->files[number - 3];
}

static int copy_path(struct process *p, uint32_t address, char path[256])
{
    for (unsigned i = 0; i < 256; ++i) {
        const uint8_t *byte = user(p, address + i, 1, false);
        if (!byte)
            return -EFAULT;
        path[i] = *byte;
        if (!path[i])
            return i ? 0 : -ENOENT;
    }
    return -ENAMETOOLONG;
}

static bool same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static const char *virtual_name(const char *path)
{
    while (*path == '/')
        ++path;
    return path;
}

static unsigned node(const char *path)
{
    if (same(path, "null"))
        return 1;
    if (same(path, "zero"))
        return 2;
    return same(path, "console") ? 3 : 0;
}

static uint32_t start_dir(struct process *p, enum volume volume)
{
    return volume == p->cwd_volume ? p->cwd : EXT2_ROOT;
}

static int openat(struct process *p, uint32_t dirfd, uint32_t address, uint32_t flags)
{
    if ((int32_t)dirfd != AT_FDCWD)
        return -EBADF;
    if ((flags & 3) == 3 || flags & ~(3u | O_CREAT | O_EXCL | O_TRUNC | O_APPEND))
        return -EINVAL;
    unsigned slot = 0;
    while (slot < sizeof(p->files) / sizeof(p->files[0]) && p->files[slot].inode)
        ++slot;
    if (slot == sizeof(p->files) / sizeof(p->files[0]))
        return -EMFILE;

    char path[256];
    int error = copy_path(p, address, path);
    if (error)
        return error;
    struct vfs_path route = vfs_resolve(path, p->cwd_volume, true);
    if (!vfs_select(route.volume))
        return -ENODEV;
    uint32_t number;
    if (route.volume == VFS_A || route.volume == VFS_DEV || route.volume == VFS_TMP) {
        const char *name = virtual_name(route.name);
        if (!*name)
            return -EISDIR;
        if (route.volume == VFS_A)
            return flags & (3 | O_CREAT | O_TRUNC) ? -EROFS : -ENOENT;
        if (route.volume == VFS_DEV) {
            number = node(name);
            if (!number)
                return -ENOENT;
            if ((flags & (O_EXCL | O_CREAT)) == (O_EXCL | O_CREAT))
                return -EEXIST;
        } else {
            unsigned len = 0;
            while (name[len] && name[len] != '/' && len < sizeof(p->tmp_name))
                ++len;
            if (len >= sizeof(p->tmp_name))
                return -ENAMETOOLONG;
            if (name[len] || same(name, ".") || same(name, ".."))
                return -ENOENT;
            if (same(name, p->tmp_name)) {
                if ((flags & (O_EXCL | O_CREAT)) == (O_EXCL | O_CREAT))
                    return -EEXIST;
                if (flags & O_TRUNC)
                    p->tmp_size = 0;
            } else {
                if (!(flags & O_CREAT))
                    return -ENOENT;
                if (*p->tmp_name)
                    return -ENOSPC;
                for (unsigned i = 0; i <= len; ++i)
                    p->tmp_name[i] = name[i];
            }
            number = 1;
        }
    } else {
        struct ext2_inode inode;
        uint32_t start = start_dir(p, route.volume);
        enum ext2_result result = ext2_lookup(start, route.name, &inode);
        if (result == EXT2_MISSING && (flags & O_CREAT)) {
            result = ext2_create(start, route.name, 0, 0, false);
            if (result == EXT2_OK)
                result = ext2_lookup(start, route.name, &inode);
        } else if (result == EXT2_OK && (flags & (O_EXCL | O_CREAT)) == (O_EXCL | O_CREAT)) {
            return -EEXIST;
        }
        if (result != EXT2_OK)
            return fs_error(result);
        if (inode.kind != EXT2_REGULAR)
            return inode.kind == EXT2_DIRECTORY ? -EISDIR : -EACCES;
        if ((flags & 3) || (flags & O_TRUNC)) {
            if (!ext2_writable())
                return -EROFS;
            if (!(inode.mode & 0222))
                return -EACCES;
            if (flags & O_TRUNC) {
                result = ext2_write_chunk(&inode, 0, 0, 0);
                if (result != EXT2_OK)
                    return fs_error(result);
            }
        }
        number = inode.number;
    }
    p->files[slot] = (struct file){number, 0, flags, route.volume};
    return slot + 3;
}

static int io_read(struct process *p, uint32_t number, uint32_t address, uint32_t size)
{
    struct file *file = fd(p, number);
    if ((!file && (number != 0 || !(p->standard & 1))) ||
        (file && (file->flags & 3) == 1))
        return -EBADF;
    uint8_t *target = user(p, address, size, true);
    if (!target)
        return -EFAULT;
    if (!size)
        return 0;
    if (file && file->volume == VFS_TMP) {
        unsigned count = file->position < p->tmp_size ? p->tmp_size - file->position : 0;
        if (count > size)
            count = size;
        if (count)
            copy(target, p->tmp_data + file->position, count);
        file->position += count;
        return count;
    }
    if (file && file->volume == VFS_DEV && file->inode != 3) {
        if (file->inode == 1)
            return 0;
        for (unsigned i = 0; i < size; ++i)
            target[i] = 0;
        return size;
    }
    if (!file || file->volume == VFS_DEV) {
        unsigned count = 0;
        do {
            unsigned c = platform_getc() & 0xff;
            if (!c)
                continue;
            target[count++] = c == '\r' ? '\n' : c;
            if (c == '\r' || c == '\n')
                break;
        } while (count < size);
        return count;
    }
    vfs_select(file->volume);
    struct ext2_inode inode;
    if (!ext2_get_inode(file->inode, &inode))
        return -EIO;
    unsigned done = 0;
    while (done < size) {
        const uint8_t *chunk;
        unsigned bytes;
        enum ext2_result result = ext2_read_chunk(&inode, file->position, &chunk, &bytes);
        if (result != EXT2_OK)
            return done ? (int)done : fs_error(result);
        if (!bytes)
            break;
        if (bytes > size - done)
            bytes = size - done;
        copy(target + done, chunk, bytes);
        done += bytes;
        file->position += bytes;
    }
    return done;
}

static int io_write(struct process *p, uint32_t number, uint32_t address, uint32_t size)
{
    struct file *file = fd(p, number);
    if ((!file && (number < 1 || number > 2 || !(p->standard & (1u << number)))) ||
        (file && !(file->flags & 3)))
        return -EBADF;
    const uint8_t *source = user(p, address, size, false);
    if (!source)
        return -EFAULT;
    if (file && file->volume == VFS_TMP) {
        if (!size)
            return 0;
        if (file->flags & O_APPEND)
            file->position = p->tmp_size;
        if (file->position >= sizeof(p->tmp_data))
            return -ENOSPC;
        if (size > sizeof(p->tmp_data) - file->position)
            size = sizeof(p->tmp_data) - file->position;
        for (unsigned i = p->tmp_size; i < file->position; ++i)
            p->tmp_data[i] = 0;
        copy(p->tmp_data + file->position, source, size);
        file->position += size;
        if (p->tmp_size < file->position)
            p->tmp_size = file->position;
        return size;
    }
    if (!file || file->volume == VFS_DEV) {
        if (!file || file->inode == 3)
            print_bytes(source, size);
        return size;
    }
    vfs_select(file->volume);
    unsigned done = 0;
    while (done < size) {
        struct ext2_inode inode;
        if (!ext2_get_inode(file->inode, &inode))
            return done ? (int)done : -EIO;
        if (file->flags & O_APPEND)
            file->position = inode.size;
        unsigned chunk = 1024 - file->position % 1024;
        if (chunk > size - done)
            chunk = size - done;
        enum ext2_result result = ext2_write_chunk(&inode, file->position, source + done, chunk);
        if (result != EXT2_OK)
            return done ? (int)done : fs_error(result);
        done += chunk;
        file->position += chunk;
    }
    return done;
}

static int seek(struct process *p, const struct native_frame *r)
{
    struct file *file = fd(p, r->ebx);
    if (!file)
        return r->ebx < 3 && (p->standard & (1u << r->ebx)) ? -ESPIPE : -EBADF;
    if (file->volume == VFS_DEV)
        return -ESPIPE;
    uint8_t *result = user(p, r->esi, 8, true);
    if (!result)
        return -EFAULT;
    uint32_t origin = 0;
    if (r->edi == 1) {
        origin = file->position;
    } else if (r->edi == 2) {
        if (file->volume == VFS_TMP) {
            origin = p->tmp_size;
        } else {
            vfs_select(file->volume);
            struct ext2_inode inode;
            if (!ext2_get_inode(file->inode, &inode))
                return -EIO;
            origin = inode.size;
        }
    } else if (r->edi != 0) {
        return -EINVAL;
    }
    uint32_t target = origin + r->edx;
    if (r->ecx + (target < origin) != 0)
        return -EINVAL;
    file->position = target;
    write32(result, target);
    write32(result + 4, 0);
    return 0;
}

static void map(struct process *p, unsigned page)
{
    uint8_t *bytes = p->space.image + page * NATIVE_PAGE;
    for (unsigned i = 0; i < NATIVE_PAGE; ++i)
        bytes[i] = 0;
    native_page(&p->space, page, true);
}

static uint32_t set_brk(struct process *p, uint32_t requested)
{
    if (requested < HEAP_START || requested > HEAP_END)
        return p->brk;
    unsigned old = (p->brk + NATIVE_PAGE - 1) / NATIVE_PAGE;
    unsigned next = (requested + NATIVE_PAGE - 1) / NATIVE_PAGE;
    while (old < next)
        map(p, old++);
    while (old > next)
        native_page(&p->space, --old, false);
    p->brk = requested;
    return requested;
}

static int mmap2(struct process *p, const struct native_frame *r)
{
    if (r->ebx || !r->ecx || r->ecx > MMAP_END - HEAP_END || r->edx != 3 ||
        r->esi != 0x22 || r->edi != UINT32_MAX || r->ebp)
        return -EINVAL;
    unsigned count = (r->ecx + NATIVE_PAGE - 1) / NATIVE_PAGE;
    for (unsigned slot = HEAP_END / NATIVE_PAGE; slot + count <= MMAP_END / NATIVE_PAGE; ++slot) {
        unsigned free = 0;
        while (free < count && !p->space.pages[slot + free])
            ++free;
        if (free == count) {
            for (unsigned i = 0; i < count; ++i)
                map(p, slot + i);
            return slot * NATIVE_PAGE;
        }
    }
    return -ENOMEM;
}

static int path_info(struct process *p, struct vfs_path route, struct ext2_inode *info)
{
    if (!vfs_select(route.volume))
        return -ENODEV;
    if (route.volume == VFS_B || route.volume == VFS_D) {
        enum ext2_result result = ext2_lookup(start_dir(p, route.volume), route.name, info);
        return result == EXT2_OK ? 0 : fs_error(result);
    }
    const char *name = virtual_name(route.name);
    info->size = 0;
    info->number = 1;
    if (!*name || same(name, ".")) {
        info->kind = EXT2_DIRECTORY;
        info->mode = 0040755;
        return 0;
    }
    if (route.volume == VFS_TMP && same(name, p->tmp_name)) {
        info->kind = EXT2_REGULAR;
        info->mode = 0100644;
        info->size = p->tmp_size;
        return 0;
    }
    if (route.volume == VFS_DEV && node(name)) {
        info->kind = 0020000;
        info->mode = 0020666;
        info->number = node(name);
        return 0;
    }
    return -ENOENT;
}

static int store_stat(struct process *p, uint32_t address, enum volume volume,
                      const struct ext2_inode *info)
{
    /* Linux i386 stat64, with 64-bit fields aligned to four bytes. */
    uint8_t *result = user(p, address, 96, true);
    if (!result)
        return -EFAULT;
    for (unsigned i = 0; i < 96; ++i)
        result[i] = 0;
    write32(result, volume + 1);
    write32(result + 12, info->number);
    write32(result + 16, info->mode);
    write32(result + 20, info->kind == EXT2_DIRECTORY ? 2 : 1);
    write32(result + 44, info->size);
    write32(result + 52, 1024);
    write32(result + 56, (info->size + 511) / 512);
    write32(result + 88, info->number);
    return 0;
}

static int by_path(struct process *p, struct native_frame *r)
{
    bool stat = r->eax == 300;
    if (stat && ((int32_t)r->ebx != AT_FDCWD || (r->esi & ~0x100u)))
        return -EINVAL;
    char name[256];
    int error = copy_path(p, stat ? r->ecx : r->ebx, name);
    if (error)
        return error;
    struct vfs_path route = vfs_resolve(name, p->cwd_volume, true);
    struct ext2_inode info;
    error = path_info(p, route, &info);
    if (error)
        return error;
    if (stat)
        return store_stat(p, r->edx, route.volume, &info);
    if (info.kind != EXT2_DIRECTORY)
        return -ENOTDIR;
    p->cwd_volume = route.volume;
    p->cwd = info.number;
    return 0;
}

struct exec_stack {
    uint8_t bytes[768];
    uint32_t cursor;
};

static bool add_exec_string(struct exec_stack *stack, const char *text,
                            uint32_t *address)
{
    unsigned size = 1;
    while (text[size - 1])
        ++size;
    if (size > stack->cursor)
        return false;
    stack->cursor -= size;
    copy(stack->bytes + stack->cursor, (const uint8_t *)text, size);
    *address = NATIVE_PAGES * NATIVE_PAGE - sizeof(stack->bytes) + stack->cursor;
    return true;
}

static int exec_vector(struct process *p, struct exec_stack *stack, uint32_t vector,
                       const char *fallback, uint32_t *list, unsigned *count)
{
    *count = 0;
    if (!vector) {
        if (!add_exec_string(stack, fallback, list))
            return -EINVAL;
        *count = 1;
        return 0;
    }
    for (unsigned i = 0; i <= 8; ++i) {
        const uint8_t *slot = user(p, vector + i * 4, 4, false);
        if (!slot)
            return -EFAULT;
        uint32_t address = read32(slot);
        if (!address)
            return 0;
        if (i == 8)
            return -EINVAL;
        char text[256];
        int result = copy_path(p, address, text);
        if (result && result != -ENOENT)
            return result;
        if (!add_exec_string(stack, text, &list[i]))
            return -EINVAL;
        ++*count;
    }
    return -EINVAL;
}

static int exec_image(struct process *p, const char *path, uint32_t argv, uint32_t envp)
{
    struct vfs_path route = vfs_resolve(path, p->cwd_volume, true);
    struct ext2_inode info;
    int error = path_info(p, route, &info);
    if (error)
        return error;
    if ((route.volume != VFS_B && route.volume != VFS_D) || !(info.mode & 0111))
        return -EACCES;
    struct exec_stack stack;
    stack.cursor = sizeof(stack.bytes);
    uint32_t args[8], env[8];
    unsigned argc, envc;
    error = exec_vector(p, &stack, argv, path, args, &argc);
    if (!error)
        error = exec_vector(p, &stack, envp, "PATH=/bin", env, &envc);
    if (error)
        return error;
    unsigned words = 1 + argc + 1 + envc + 1 + 4;
    if (stack.cursor < words * 4)
        return -EINVAL;
    unsigned begin = (stack.cursor - words * 4) & ~15u;
    uint8_t *position = stack.bytes + begin;
    write32(position, argc);
    position += 4;
    for (unsigned i = 0; i < argc; ++i, position += 4)
        write32(position, args[i]);
    write32(position, 0);
    position += 4;
    for (unsigned i = 0; i < envc; ++i, position += 4)
        write32(position, env[i]);
    write32(position, 0);
    write32(position + 4, 6); /* AT_PAGESZ */
    write32(position + 8, NATIVE_PAGE);
    write32(position + 12, 0);
    write32(position + 16, 0);
    for (unsigned i = begin + words * 4; i < stack.cursor; ++i)
        stack.bytes[i] = 0;

    if (!load_elf(&p->space, &info))
        return -ENOEXEC;
    p->brk = HEAP_START;
    p->tid_address = 0;
    p->tls_set = false;
    p->space.frame.esp = NATIVE_PAGES * NATIVE_PAGE - sizeof(stack.bytes) + begin;
    copy(p->space.image + p->space.frame.esp, stack.bytes + begin, sizeof(stack.bytes) - begin);
    return 0;
}

static int syscall(struct process *p)
{
    struct native_frame *r = &p->space.frame;
    switch (r->eax) {
    case 3: return io_read(p, r->ebx, r->ecx, r->edx);
    case 4: return io_write(p, r->ebx, r->ecx, r->edx);
    case 6: {
        struct file *file = fd(p, r->ebx);
        if (file)
            file->inode = 0;
        else if (r->ebx < 3 && (p->standard & (1u << r->ebx)))
            p->standard &= ~(1u << r->ebx);
        else
            return -EBADF;
        return 0;
    }
    case 11: {
        char path[256];
        int error = copy_path(p, r->ebx, path);
        if (error)
            return error;
        return exec_image(p, path, r->ecx, r->edx);
    }
    case 12: case 300: return by_path(p, r);
    case 45: return set_brk(p, r->ebx);
    case 91: {
        uint32_t address = r->ebx, length = r->ecx;
        if (address < HEAP_END || address >= MMAP_END || address % NATIVE_PAGE ||
            !length || length > MMAP_END - address)
            return -EINVAL;
        for (unsigned off = 0; off < length; off += NATIVE_PAGE)
            native_page(&p->space, (address + off) / NATIVE_PAGE, false);
        return 0;
    }
    case 140: return seek(p, r);
    case 146: {
        if (r->edx > 16)
            return -EINVAL;
        const uint8_t *iov = user(p, r->ecx, r->edx * 8, false);
        if (!iov)
            return -EFAULT;
        int total = 0;
        for (unsigned i = 0; i < r->edx; ++i) {
            uint32_t size = read32(iov + 4);
            int count = io_write(p, r->ebx, read32(iov), size);
            if (count < 0)
                return total ? total : count;
            total += count;
            if ((uint32_t)count < size)
                break;
            iov += 8;
        }
        return total;
    }
    case 192: return mmap2(p, r);
    case 197: {
        struct ext2_inode info;
        struct file *file = fd(p, r->ebx);
        if (file && (file->volume == VFS_B || file->volume == VFS_D)) {
            vfs_select(file->volume);
            if (!ext2_get_inode(file->inode, &info))
                return -EIO;
            return store_stat(p, r->ecx, file->volume, &info);
        }
        if (!file && (r->ebx > 2 || !(p->standard & (1u << r->ebx))))
            return -EBADF;
        enum volume volume = file ? file->volume : VFS_DEV;
        info.number = file ? file->inode : 3;
        info.kind = volume == VFS_TMP ? EXT2_REGULAR : 0020000;
        info.mode = info.kind | (volume == VFS_TMP ? 0644 : 0666);
        info.size = volume == VFS_TMP ? p->tmp_size : 0;
        return store_stat(p, r->ecx, volume, &info);
    }
    case 243: {
        uint8_t *tls = user(p, r->ebx, 16, true);
        if (!tls)
            return -EFAULT;
        uint32_t slot = read32(tls), base = read32(tls + 4);
        uint32_t limit = read32(tls + 8), flags = read32(tls + 12);
        if ((slot != UINT32_MAX && slot != 11) || (flags & ~0x51u) ||
            !(flags & 1) || limit > 0xfffff || !user(p, base, 1, false))
            return -EINVAL;
        if (slot == UINT32_MAX && p->tls_set)
            return -3;
        if (flags & 0x10)
            limit = (limit << 12) | 0xfff;
        uint32_t max = NATIVE_PAGES * NATIVE_PAGE - 1 - base;
        native_tls(base, limit < max ? limit : max);
        p->tls_set = true;
        write32(tls, 11);
        return 0;
    }
    case 258:
        if (r->ebx && !user(p, r->ebx, 4, true))
            return -EFAULT;
        p->tid_address = r->ebx;
        return 1;
    case 295: return openat(p, r->ebx, r->ecx, r->edx);
    default: return -ENOSYS;
    }
}

void native_start(uint32_t directory, const char *path, bool quiet)
{
    struct process process;
    for (unsigned i = 0; i < sizeof(process); ++i)
        ((volatile uint8_t *)&process)[i] = 0;
    process.cwd = directory;
    process.cwd_volume = VFS_D;
    process.brk = HEAP_START;
    process.standard = 7;
    if (!native_space_init(&process.space)) {
        if (!quiet)
            print("Native runtime unavailable\r\n");
        return;
    }
    int status = exec_image(&process, path, 0, 0);
    if (status) {
        if (!quiet || (status != -ENOENT && status != -ENODEV)) {
            print("Cannot load ");
            print(path);
            print("\r\n");
        }
        return;
    }
    for (;;) {
        uint32_t event = native_step(&process.space);
        if (event != NATIVE_TRAP) {
            print("Native program fault ");
            number(event & 0xff);
            print("\r\n");
            break;
        }
        struct native_frame *frame = &process.space.frame;
        if (frame->eax == 1 || frame->eax == 252) {
            uint8_t *tid = user(&process, process.tid_address, 4, true);
            if (tid)
                write32(tid, 0);
            print("Native exit ");
            number(frame->ebx & 0xff);
            print("\r\n");
            break;
        }
        frame->eax = syscall(&process);
    }
}
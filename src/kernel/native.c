#include "native.h"

#include "bytes.h"
#include "console.h"
#include "ext2.h"
#include "vfs.h"
#include "x86_domains.h"
#include "platform.h"

enum {
    ENOENT = 2, EIO = 5, ENOEXEC = 8, EBADF = 9, ENOMEM = 12, EACCES = 13, EFAULT = 14,
    EEXIST = 17, ENODEV = 19, ENOTDIR = 20, EISDIR = 21, EINVAL = 22, EMFILE = 24,
    ENOSPC = 28, ESPIPE = 29, EROFS = 30, ENAMETOOLONG = 36, ENOSYS = 38,
    EINTR = 4, ESRCH = 3, ECHILD = 10, EAGAIN = 11, EPIPE = 32, ENOTTY = 25, ERANGE = 34,
    AT_FDCWD = -100, FD_COUNT = 16, FILE_COUNT = 64, TASKS = DDOS_X86_NATIVE_TASKS,
    O_CREAT = 0x40, O_EXCL = 0x80, O_TRUNC = 0x200, O_APPEND = 0x400,
    EAFNOSUPPORT = 97, EADDRINUSE = 98, ENOTCONN = 107, ECONNREFUSED = 111,
    BLOCKED = -4096, RESUMED = -4097, PIPE_COUNT = 8, PIPE_BYTES = 512,
    POLLIN = 1, POLLOUT = 4, POLLERR = 8, POLLHUP = 16, POLLNVAL = 32,
    SIGCHLD = 17, SIGCONT = 18, SIGKILL = 9, SIGSTOP = 19, SIGPIPE = 13,
    SA_RESTORER = 0x04000000, SA_RESTART = 0x10000000,
    O_NONBLOCK = 0x800, O_NOCTTY = 0x100, O_LARGEFILE = 0x8000, O_CLOEXEC = 0x80000,
};

enum task_state { FREE, RUNNABLE, WAIT_CHILD, WAIT_IO, ZOMBIE };

struct file {
    uint32_t inode, position, flags;
    enum volume volume;
    unsigned refs;
};

struct signal_action {
    uint32_t handler, flags, restorer, mask[2];
};

struct process {
    struct native_space space;
    uint32_t cwd, brk, heap_start, mmap_start, mmap_end, tid_address;
    uint32_t pid, parent, group, session, status;
    struct signal_action actions[32];
    uint32_t pending, mask[2], saved_mask[2];
    struct native_frame saved_frame;
    _Alignas(16) uint8_t saved_fpu[512];
    bool in_signal;
    enum volume cwd_volume;
    enum task_state state;
    uint32_t cloexec, deadline;
    bool deadline_set;
    bool tls_set;
    uint8_t files[FD_COUNT];
};

struct pipe_queue {
    unsigned start, size;
    uint8_t bytes[PIPE_BYTES];
};
struct pipe {
    struct pipe_queue input[2];
    char name[32];
    uint8_t ends[2];
    bool connected;
};

struct local_socket {
    char name[64];
    uint8_t accepted[PIPE_COUNT];
    unsigned count, backlog;
    bool live;
};

struct native_runtime {
    struct process tasks[TASKS];
    struct file files[FILE_COUNT];
    uint32_t next_pid, now;
    uint8_t key;
    struct pipe pipes[PIPE_COUNT];
    struct local_socket sockets[PIPE_COUNT];
    char tmp_name[32];
    uint8_t tmp_data[256];
    unsigned tmp_size;
};

_Static_assert(sizeof(struct native_runtime) <= DDOS_X86_NATIVE_METADATA_PAGES * NATIVE_PAGE,
               "runtime metadata exceeds the firmware reservation");

static struct native_runtime *runtime;

static void zero(void *target, unsigned count)
{
    volatile uint8_t *bytes = target;
    while (count--)
        *bytes++ = 0;
}

static void copy(uint8_t *to, const uint8_t *from, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i)
        to[i] = from[i];
}

static uint8_t *user(struct process *p, uint32_t address, uint32_t size, bool writable)
{
    if (!size)
        return p->space.image;
    uint32_t offset = address - p->space.base;
    if (!address || offset >= p->space.size || size > p->space.size - offset)
        return 0;
    for (unsigned i = offset / NATIVE_PAGE; i <= (offset + size - 1) / NATIVE_PAGE; ++i) {
        uint32_t flags = p->space.access[i];
        if (!(flags & 1) || (writable && !(flags & 2)))
            return 0;
    }
    return p->space.image + offset;
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
    unsigned slot = number < FD_COUNT ? p->files[number] : 0;
    return slot ? &runtime->files[slot - 1] : 0;
}

static void release_file(struct file *file)
{
    if (--file->refs)
        return;
    if (file->volume == VFS_PIPE) {
        runtime->pipes[file->inode - 1].ends[file->position] = 0;
    } else if (file->volume == VFS_SOCKET) {
        struct local_socket *socket = &runtime->sockets[file->inode - 1];
        for (unsigned i = 0; i < socket->count; ++i) {
            struct file *pending = &runtime->files[socket->accepted[i] - 1];
            --pending->refs;
            runtime->pipes[pending->inode - 1].ends[0] = 0;
        }
        socket->live = false;
    }
}

static void close_fd(struct process *p, unsigned number)
{
    struct file *file = fd(p, number);
    if (file)
        release_file(file);
    p->files[number] = 0;
    p->cloexec &= ~(1u << number);
}

static unsigned available_fd(struct process *p, unsigned start)
{
    while (start < FD_COUNT && p->files[start])
        ++start;
    return start;
}

static unsigned available_file(void)
{
    unsigned slot = 0;
    while (slot < FILE_COUNT && runtime->files[slot].refs)
        ++slot;
    return slot;
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
    if (same(path, "fb0") && native_framebuffer())
        return 4;
    return same(path, "console") || same(path, "tty") ? 3 : 0;
}

static uint32_t start_dir(struct process *p, enum volume volume)
{
    return volume == p->cwd_volume ? p->cwd : EXT2_ROOT;
}

static struct pipe *free_pipe(void)
{
    for (unsigned i = 0; i < PIPE_COUNT; ++i) {
        struct pipe *pipe = &runtime->pipes[i];
        if (!pipe->ends[0] && !pipe->ends[1]) {
            zero(pipe, sizeof(*pipe));
            return pipe;
        }
    }
    return 0;
}

static int pipe_endpoint(struct process *p, struct pipe *pipe, unsigned end, unsigned flags)
{
    unsigned number = available_fd(p, 0), slot = available_file();
    if (number == FD_COUNT || slot == FILE_COUNT)
        return -EMFILE;
    runtime->files[slot] = (struct file){pipe - runtime->pipes + 1, end,
                                       flags & ~O_CLOEXEC, VFS_PIPE, 1};
    p->files[number] = pipe->ends[end] = slot + 1;
    if (flags & O_CLOEXEC)
        p->cloexec |= 1u << number;
    return number;
}

static int make_pipe(struct process *p, unsigned address, unsigned flags)
{
    if (flags & ~(O_CLOEXEC | O_NONBLOCK))
        return -EINVAL;
    uint8_t *out = user(p, address, 8, true);
    if (!out)
        return -EFAULT;
    struct pipe *pipe = free_pipe();
    if (!pipe)
        return -ENOSPC;
    int read_end = pipe_endpoint(p, pipe, 0, flags);
    if (read_end < 0)
        return read_end;
    int write_end = pipe_endpoint(p, pipe, 1, flags | 1);
    if (write_end < 0) {
        close_fd(p, read_end);
        return write_end;
    }
    pipe->connected = true;
    write32(out, read_end);
    write32(out + 4, write_end);
    return 0;
}

static const char *named_pipe_path(const char *path)
{
    const char prefix[] = "\\\\.\\pipe\\";
    for (unsigned i = 0; i < sizeof(prefix) - 1; ++i) {
        unsigned c = (unsigned char)path[i];
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (c != (unsigned char)prefix[i])
            return 0;
    }
    return path + sizeof(prefix) - 1;
}

static int open_named_pipe(struct process *p, const char *name, unsigned flags)
{
    unsigned allowed = 2 | O_NONBLOCK | O_CLOEXEC | O_LARGEFILE | O_CREAT | O_EXCL;
    bool server = flags & O_CREAT;
    if (!*name || (flags & 3) != 2 || (flags & ~allowed) ||
        server != !!(flags & O_EXCL))
        return -EINVAL;
    char key[32];
    unsigned len = 0;
    while (name[len] && len < sizeof(key) - 1) {
        unsigned c = (unsigned char)name[len];
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return -EINVAL;
        key[len++] = c;
    }
    if (name[len])
        return -ENAMETOOLONG;
    key[len] = 0;
    for (unsigned i = 0; i < PIPE_COUNT; ++i) {
        struct pipe *pipe = &runtime->pipes[i];
        if ((!pipe->ends[0] && !pipe->ends[1]) || !same(pipe->name, key))
            continue;
        if (server)
            return -EEXIST;
        if (!pipe->ends[0] || pipe->connected)
            return -EAGAIN;
        int result = pipe_endpoint(p, pipe, 1, flags & ~(O_CREAT | O_EXCL));
        if (result >= 0)
            pipe->connected = true;
        return result;
    }
    if (!server)
        return -ENOENT;
    struct pipe *pipe = free_pipe();
    if (!pipe)
        return -ENOSPC;
    copy((uint8_t *)pipe->name, (const uint8_t *)key, len + 1);
    return pipe_endpoint(p, pipe, 0, flags & ~(O_CREAT | O_EXCL));
}

static int read_pipe(const struct file *file, uint8_t *data, unsigned count)
{
    const unsigned end = file->position;
    struct pipe *pipe = &runtime->pipes[file->inode - 1];
    struct pipe_queue *queue = &pipe->input[end];
    if (!queue->size) {
        if (pipe->connected && !pipe->ends[end ^ 1])
            return 0;
        return file->flags & O_NONBLOCK ? -EAGAIN : BLOCKED;
    }
    if (count > queue->size)
        count = queue->size;
    for (unsigned i = 0; i < count; ++i)
        data[i] = queue->bytes[(queue->start + i) % PIPE_BYTES];
    queue->start = (queue->start + count) % PIPE_BYTES;
    queue->size -= count;
    return count;
}

static int write_pipe(struct process *p, const struct file *file, const uint8_t *data, unsigned count)
{
    unsigned peer = file->position ^ 1;
    struct pipe *pipe = &runtime->pipes[file->inode - 1];
    struct pipe_queue *queue = &pipe->input[peer];
    if (!pipe->ends[peer]) {
        if (pipe->connected) {
            p->pending |= 1u << (SIGPIPE - 1);
            return -EPIPE;
        }
        return file->flags & O_NONBLOCK ? -EAGAIN : BLOCKED;
    }
    unsigned room = PIPE_BYTES - queue->size;
    if (!room || (count <= PIPE_BYTES && room < count))
        return file->flags & O_NONBLOCK ? -EAGAIN : BLOCKED;
    if (count > room)
        count = room;
    for (unsigned i = 0; i < count; ++i)
        queue->bytes[(queue->start + queue->size + i) % PIPE_BYTES] = data[i];
    queue->size += count;
    return count;
}

static unsigned console_key(void)
{
    while (!runtime->key) {
        unsigned scan = platform_try_getc();
        if (!scan)
            return 0;
        runtime->key = scan & 0xff;
    }
    return runtime->key;
}

static int openat(struct process *p, uint32_t dirfd, uint32_t address, uint32_t flags)
{
    if ((int32_t)dirfd != AT_FDCWD)
        return -EBADF;
    if ((flags & 3) == 3 || flags & ~(3u | O_CREAT | O_EXCL | O_TRUNC | O_APPEND | O_NOCTTY | O_LARGEFILE | O_CLOEXEC | O_NONBLOCK))
        return -EINVAL;
    unsigned number_fd = available_fd(p, 0), slot = available_file();
    if (number_fd == FD_COUNT || slot == FILE_COUNT)
        return -EMFILE;

    char path[256];
    int error = copy_path(p, address, path);
    if (error)
        return error;
    const char *name = named_pipe_path(path);
    if (name)
        return open_named_pipe(p, name, flags);
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
            while (name[len] && name[len] != '/' && len < sizeof(runtime->tmp_name))
                ++len;
            if (len >= sizeof(runtime->tmp_name))
                return -ENAMETOOLONG;
            if (name[len] || same(name, ".") || same(name, ".."))
                return -ENOENT;
            if (same(name, runtime->tmp_name)) {
                if ((flags & (O_EXCL | O_CREAT)) == (O_EXCL | O_CREAT))
                    return -EEXIST;
                if (flags & O_TRUNC)
                    runtime->tmp_size = 0;
            } else {
                if (!(flags & O_CREAT))
                    return -ENOENT;
                if (*runtime->tmp_name)
                    return -ENOSPC;
                for (unsigned i = 0; i <= len; ++i)
                    runtime->tmp_name[i] = name[i];
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
    runtime->files[slot] = (struct file){number, 0, flags & ~O_CLOEXEC, route.volume, 1};
    p->files[number_fd] = slot + 1;
    if (flags & O_CLOEXEC)
        p->cloexec |= 1u << number_fd;
    return number_fd;
}

static int io_read(struct process *p, uint32_t number, uint32_t address, uint32_t size)
{
    struct file *file = fd(p, number);
    if (!file || (file->flags & 3) == 1)
        return -EBADF;
    uint8_t *target = user(p, address, size, true);
    if (!target)
        return -EFAULT;
    if (!size)
        return 0;
    if (file->volume == VFS_SOCKET)
        return -ENOTCONN;
    if (file->volume == VFS_PIPE)
        return read_pipe(file, target, size);
    if (file->volume == VFS_TMP) {
        unsigned count = file->position < runtime->tmp_size ? runtime->tmp_size - file->position : 0;
        if (count > size)
            count = size;
        if (count)
            copy(target, runtime->tmp_data + file->position, count);
        file->position += count;
        return count;
    }
    if (file->volume == VFS_DEV && file->inode != 3) {
        if (file->inode == 4)
            return -EINVAL;
        if (file->inode == 1)
            return 0;
        for (unsigned i = 0; i < size; ++i)
            target[i] = 0;
        return size;
    }
    if (file->volume == VFS_DEV) {
        unsigned c = console_key();
        if (!c)
            return file->flags & O_NONBLOCK ? -EAGAIN : BLOCKED;
        runtime->key = 0;
        *target = c == '\r' ? '\n' : c;
        return 1;
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
    if (!file || !(file->flags & 3))
        return -EBADF;
    const uint8_t *source = user(p, address, size, false);
    if (!source)
        return -EFAULT;
    if (file->volume == VFS_SOCKET)
        return -ENOTCONN;
    if (file->volume == VFS_PIPE)
        return size ? write_pipe(p, file, source, size) : 0;
    if (file->volume == VFS_TMP) {
        if (!size)
            return 0;
        if (file->flags & O_APPEND)
            file->position = runtime->tmp_size;
        if (file->position >= sizeof(runtime->tmp_data))
            return -ENOSPC;
        if (size > sizeof(runtime->tmp_data) - file->position)
            size = sizeof(runtime->tmp_data) - file->position;
        for (unsigned i = runtime->tmp_size; i < file->position; ++i)
            runtime->tmp_data[i] = 0;
        copy(runtime->tmp_data + file->position, source, size);
        file->position += size;
        if (runtime->tmp_size < file->position)
            runtime->tmp_size = file->position;
        return size;
    }
    if (file->volume == VFS_DEV) {
        if (file->inode == 4)
            return -EINVAL;
        if (file->inode == 3)
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
        return -EBADF;
    if (file->volume == VFS_DEV || file->volume == VFS_PIPE || file->volume == VFS_SOCKET)
        return -ESPIPE;
    uint8_t *result = user(p, r->esi, 8, true);
    if (!result)
        return -EFAULT;
    uint32_t origin = 0;
    if (r->edi == 1) {
        origin = file->position;
    } else if (r->edi == 2) {
        if (file->volume == VFS_TMP) {
            origin = runtime->tmp_size;
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
    native_page(&p->space, page, 3);
}

static uint32_t set_brk(struct process *p, uint32_t requested)
{
    if (requested < p->heap_start || requested > p->mmap_start)
        return p->brk;
    unsigned old = (p->brk - p->space.base + NATIVE_PAGE - 1) / NATIVE_PAGE;
    unsigned next = (requested - p->space.base + NATIVE_PAGE - 1) / NATIVE_PAGE;
    while (old < next)
        map(p, old++);
    while (old > next)
        native_page(&p->space, --old, 0);
    p->brk = requested;
    return requested;
}

static int mmap2(struct process *p, const struct native_frame *r)
{
    if ((r->esi & 3) == 1) {
        struct file *file = fd(p, r->edi);
        const struct native_fb *fb = native_framebuffer();
        if (!file || file->volume != VFS_DEV || file->inode != 4 || !fb)
            return -ENODEV;
        if (p->space.fb_access || r->ebx || r->ecx != fb->size || r->ebp ||
            r->esi != 1 || (r->edx != 1 && r->edx != 3))
            return -EINVAL;
        if ((file->flags & 3) == 1 || (r->edx == 3 && (file->flags & 3) != 2))
            return -EACCES;
        p->space.fb_access = r->edx;
        return p->space.base + NATIVE_FB_OFFSET;
    }
    if (r->ebx || !r->ecx || r->ecx > p->mmap_end - p->mmap_start || r->edx != 3 ||
        r->esi != 0x22 || r->edi != UINT32_MAX || r->ebp)
        return -EINVAL;
    unsigned count = (r->ecx + NATIVE_PAGE - 1) / NATIVE_PAGE;
    unsigned first = (p->mmap_start - p->space.base) / NATIVE_PAGE;
    unsigned end = (p->mmap_end - p->space.base) / NATIVE_PAGE;
    for (unsigned slot = first; slot + count <= end; ++slot) {
        unsigned free = 0;
        while (free < count && !p->space.access[slot + free])
            ++free;
        if (free == count) {
            for (unsigned i = 0; i < count; ++i)
                map(p, slot + i);
            return p->space.base + slot * NATIVE_PAGE;
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
    if (route.volume == VFS_TMP && same(name, runtime->tmp_name)) {
        info->kind = EXT2_REGULAR;
        info->mode = 0100644;
        info->size = runtime->tmp_size;
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

static int make_directory(struct process *p, const struct native_frame *r)
{
    bool at = r->eax == 296;
    if (at && (int32_t)r->ebx != AT_FDCWD)
        return -EBADF;
    char name[256];
    int error = copy_path(p, at ? r->ecx : r->ebx, name);
    if (error)
        return error;
    struct vfs_path route = vfs_resolve(name, p->cwd_volume, true);
    if (!vfs_select(route.volume))
        return -ENODEV;
    if (route.volume == VFS_TMP || route.volume == VFS_DEV)
        return *virtual_name(route.name) ? -EROFS : -EEXIST;
    if (route.volume == VFS_A)
        return -EROFS;
    enum ext2_result result = ext2_create(start_dir(p, route.volume), route.name, 0, 0, true);
    return result == EXT2_OK ? 0 : fs_error(result);
}

static int by_path(struct process *p, struct native_frame *r)
{
    bool stat = r->eax != 12;
    bool at = r->eax == 300;
    if (at && ((int32_t)r->ebx != AT_FDCWD || (r->esi & ~0x100u)))
        return -EINVAL;
    char name[256];
    int error = copy_path(p, at ? r->ecx : r->ebx, name);
    if (error)
        return error;
    struct vfs_path route = vfs_resolve(name, p->cwd_volume, true);
    struct ext2_inode info;
    error = path_info(p, route, &info);
    if (error)
        return error;
    if (stat)
        return store_stat(p, at ? r->edx : r->ecx, route.volume, &info);
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
    *address = stack->cursor;
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

static int duplicate_fd(struct process *p, unsigned from, unsigned to, bool cloexec)
{
    struct file *file = fd(p, from);
    if (!file || to >= FD_COUNT)
        return -EBADF;
    if (from == to)
        return to;
    close_fd(p, to);
    ++file->refs;
    p->files[to] = p->files[from];
    if (cloexec)
        p->cloexec |= 1u << to;
    return to;
}

static int file_control(struct process *p, const struct native_frame *r)
{
    struct file *file = fd(p, r->ebx);
    if (!file)
        return -EBADF;
    unsigned mask = 1u << r->ebx;
    switch (r->ecx) {
    case 0: case 1030: { /* F_DUPFD, F_DUPFD_CLOEXEC */
        if (r->edx >= FD_COUNT)
            return -EINVAL;
        unsigned to = available_fd(p, r->edx);
        return to == FD_COUNT ? -EMFILE : duplicate_fd(p, r->ebx, to, r->ecx == 1030);
    }
    case 1: return !!(p->cloexec & mask);
    case 2:
        p->cloexec = (p->cloexec & ~mask) | ((r->edx & 1) ? mask : 0);
        return 0;
    case 3: return file->flags & (3 | O_APPEND | O_LARGEFILE | O_NONBLOCK);
    case 4: /* F_SETFL */
        file->flags = (file->flags & ~(O_APPEND | O_NONBLOCK)) | (r->edx & (O_APPEND | O_NONBLOCK));
        return 0;
    default: return -EINVAL;
    }
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
    uint32_t args[10], env[8];
    unsigned argc, envc;
    error = exec_vector(p, &stack, argv, path, args, &argc);
    if (!error)
        error = exec_vector(p, &stack, envp, "PATH=/bin", env, &envc);
    if (error)
        return error;
    if (info.kind != EXT2_REGULAR || !info.size)
        return -ENOEXEC;
    const uint8_t *first;
    unsigned length;
    if (ext2_read_chunk(&info, 0, &first, &length) != EXT2_OK)
        return -EIO;
    if (length >= 2 && first[0] == '#' && first[1] == '!') {
        char interpreter[128];
        unsigned i = 2, n = 0;
        while (i < length && (first[i] == ' ' || first[i] == 9))
            ++i;
        while (i < length && first[i] > ' ' && n < sizeof(interpreter) - 1)
            interpreter[n++] = first[i++];
        while (i < length && (first[i] == ' ' || first[i] == 9 || first[i] == 13))
            ++i;
        if (!n || i == length || interpreter[0] != '/' || first[i] != '\n')
            return -ENOEXEC;
        interpreter[n] = 0;
        uint32_t script, command;
        if (!add_exec_string(&stack, interpreter, &command) ||
            !add_exec_string(&stack, path, &script))
            return -EINVAL;
        for (unsigned j = argc; j > 1; --j)
            args[j] = args[j - 1];
        argc = argc ? argc + 1 : 2;
        args[0] = command;
        args[1] = script;
        route = vfs_resolve(interpreter, p->cwd_volume, true);
        error = path_info(p, route, &info);
        if (error)
            return error;
        if (!(info.mode & 0111))
            return -EACCES;
    }
    unsigned words = 1 + argc + 1 + envc + 1 + 10;
    if (stack.cursor < words * 4)
        return -EINVAL;
    unsigned begin = (stack.cursor - words * 4) & ~15u;

    struct native_elf elf;
    struct native_space *space = &p->space;
    if (!native_load_elf(space, &info, &elf))
        return -ENOEXEC;
    uint32_t strings = space->base + space->size - sizeof(stack.bytes);
    uint8_t *position = stack.bytes + begin;
    write32(position, argc);
    position += 4;
    for (unsigned i = 0; i < argc; ++i, position += 4)
        write32(position, strings + args[i]);
    write32(position, 0);
    position += 4;
    for (unsigned i = 0; i < envc; ++i, position += 4)
        write32(position, strings + env[i]);
    write32(position, 0);
    position += 4;
    const uint32_t aux[] = {3, elf.phdr, 4, 32, 5, elf.phnum, 6, NATIVE_PAGE, 0, 0};
    for (unsigned i = 0; i < sizeof(aux) / sizeof(aux[0]); ++i)
        write32(position + i * 4, aux[i]);
    for (unsigned i = begin + words * 4; i < stack.cursor; ++i)
        stack.bytes[i] = 0;

    for (unsigned i = 0; i < FD_COUNT; ++i) {
        if (p->cloexec & (1u << i))
            close_fd(p, i);
    }
    native_reset_fpu(space);
    space->tls_base = space->tls_limit = 0;
    space->fb_access = 0;
    p->brk = p->heap_start = elf.heap_start;
    p->mmap_end = elf.stack_bottom;
    unsigned pages = (p->mmap_end - p->heap_start) / NATIVE_PAGE;
    p->mmap_start = p->heap_start + ((pages + 1) / 2) * NATIVE_PAGE;
    p->tid_address = 0;
    p->tls_set = false;
    p->in_signal = false;
    for (unsigned i = 0; i < 32; ++i) {
        if (p->actions[i].handler > 1)
            zero(&p->actions[i], sizeof(p->actions[i]));
    }
    uint32_t cs = space->frame.cs, ss = space->frame.ss;
    for (unsigned i = 0; i < sizeof(space->frame); ++i)
        ((volatile uint8_t *)&space->frame)[i] = 0;
    space->frame.cs = cs;
    space->frame.ss = ss;
    space->frame.eip = elf.entry;
    space->frame.esp = strings + begin;
    copy(space->image + space->frame.esp - space->base,
         stack.bytes + begin, sizeof(stack.bytes) - begin);
    return 0;
}

static int fork_task(struct process *parent)
{
    unsigned slot = 0;
    while (slot < TASKS && runtime->tasks[slot].state != FREE)
        ++slot;
    if (slot == TASKS || runtime->next_pid == 0x7fffffffu)
        return -EAGAIN;
    struct process *child = &runtime->tasks[slot];
    copy((uint8_t *)child, (const uint8_t *)parent, sizeof(*child));
    native_space_init(&child->space, slot);
    copy(child->space.image, parent->space.image, parent->space.size);
    child->space.frame.eax = 0;
    child->pid = runtime->next_pid++;
    child->parent = parent->pid;
    child->tid_address = 0;
    child->pending = 0;
    child->deadline_set = false;
    for (unsigned i = 0; i < FD_COUNT; ++i) {
        struct file *file = fd(child, i);
        if (file)
            ++file->refs;
    }
    return child->pid;
}

static int wait_child(struct process *parent)
{
    const struct native_frame *r = &parent->space.frame;
    int target = r->ebx;
    if ((target <= 0 && target != -1) || (r->edx & ~1u))
        return -EINVAL;
    uint8_t *status = r->ecx ? user(parent, r->ecx, 4, true) : 0;
    uint8_t *usage = r->esi ? user(parent, r->esi, 72, true) : 0;
    if ((r->ecx && !status) || (r->esi && !usage))
        return -EFAULT;
    bool found = false;
    for (unsigned i = 0; i < TASKS; ++i) {
        struct process *child = &runtime->tasks[i];
        if (child->state == FREE || child->parent != parent->pid ||
            (target != -1 && (uint32_t)target != child->pid))
            continue;
        found = true;
        if (child->state != ZOMBIE)
            continue;
        if (status)
            write32(status, child->status);
        if (usage)
            zero(usage, 72);
        child->state = FREE;
        return child->pid;
    }
    if (!found)
        return -ECHILD;
    if (!(r->edx & 1))
        parent->state = WAIT_CHILD;
    return 0;
}

static void finish_task(struct process *p, unsigned status)
{
    uint8_t *tid = p->tid_address ? user(p, p->tid_address, 4, true) : 0;
    if (tid)
        write32(tid, 0);
    for (unsigned i = 0; i < FD_COUNT; ++i)
        close_fd(p, i);
    p->status = status;
    p->state = ZOMBIE;
    for (unsigned i = 0; i < TASKS; ++i) {
        struct process *other = &runtime->tasks[i];
        if (other->state == FREE)
            continue;
        if (other->parent == p->pid)
            other->parent = runtime->tasks[0].pid;
        if (other->pid == p->parent && other->pid != p->pid) {
            other->pending |= 1u << (SIGCHLD - 1);
            if (other->state == WAIT_CHILD) {
                other->state = RUNNABLE;
                other->space.frame.eax = wait_child(other);
            }
        }
    }
}

static unsigned fd_events(struct file *file)
{
    if (!file)
        return POLLNVAL;
    unsigned events = (file->flags & 3) != 1 ? POLLIN : 0;
    events |= (file->flags & 3) ? POLLOUT : 0;
    if (file->volume == VFS_DEV && file->inode == 3 && (events & POLLIN) && !console_key())
        events &= ~POLLIN;
    if (file->volume == VFS_SOCKET)
        return runtime->sockets[file->inode - 1].count ? POLLIN : 0;
    if (file->volume == VFS_PIPE) {
        const struct pipe *pipe = &runtime->pipes[file->inode - 1];
        unsigned end = file->position, peer = end ^ 1;
        if (!pipe->input[end].size)
            events &= ~POLLIN;
        if (!pipe->ends[peer] || pipe->input[peer].size == PIPE_BYTES)
            events &= ~POLLOUT;
        if (pipe->connected && !pipe->ends[peer])
            events |= (file->flags & 3) == 1 ? POLLERR : POLLHUP;
    }
    return events;
}

static bool timeout(struct process *p, int32_t millis)
{
    if (millis < 0)
        return false;
    if (!p->deadline_set) {
        p->deadline = runtime->now + millis;
        p->deadline_set = true;
    }
    return (int32_t)(runtime->now - p->deadline) >= 0;
}

static int poll_files(struct process *p, const struct native_frame *r)
{
    if (r->ecx > FD_COUNT)
        return -EINVAL;
    uint8_t *array = user(p, r->ebx, r->ecx * 8, true);
    if (!array)
        return -EFAULT;
    unsigned ready = 0;
    for (unsigned i = 0; i < r->ecx; ++i, array += 8) {
        int32_t number = read32(array);
        unsigned events = read16(array + 4) | POLLERR | POLLHUP | POLLNVAL;
        unsigned active = number < 0 ? 0 : fd_events(fd(p, number)) & events;
        write16(array + 6, active);
        ready += !!active;
    }
    return ready || timeout(p, r->edx) ? (int)ready : BLOCKED;
}

static int select_files(struct process *p, const struct native_frame *r)
{
    if (r->ebx > FD_COUNT)
        return -EINVAL;
    uint32_t addresses[] = {r->ecx, r->edx, r->esi};
    unsigned modes[] = {POLLIN | POLLHUP | POLLERR, POLLOUT | POLLERR, 0};
    uint8_t *sets[3];
    uint32_t result[3] = {0};
    int count = 0;
    for (unsigned i = 0; i < 3; ++i) {
        sets[i] = addresses[i] ? user(p, addresses[i], 4, true) : 0;
        if (addresses[i] && !sets[i])
            return -EFAULT;
        unsigned wanted = sets[i] ? read32(sets[i]) : 0;
        for (unsigned number = 0; number < r->ebx; ++number) {
            if (!(wanted & (1u << number)))
                continue;
            unsigned ready = fd_events(fd(p, number));
            if (ready & POLLNVAL)
                return -EBADF;
            if (ready & modes[i]) {
                result[i] |= 1u << number;
                ++count;
            }
        }
    }
    int32_t millis = -1;
    uint8_t *time = r->edi ? user(p, r->edi, 8, true) : 0;
    if (r->edi && !time)
        return -EFAULT;
    if (time) {
        unsigned seconds = read32(time), microseconds = read32(time + 4);
        if (seconds > 2147482 || microseconds >= 1000000)
            return -EINVAL;
        millis = seconds * 1000 + (microseconds + 999) / 1000;
    }
    if (!count && !timeout(p, millis))
        return BLOCKED;
    for (unsigned i = 0; i < 3; ++i) {
        if (sets[i])
            write32(sets[i], result[i]);
    }
    if (time && !count)
        zero(time, 8);
    return count;
}

static int signal_mask(struct process *p, const struct native_frame *r)
{
    if (r->esi != 8 || (r->ecx && r->ebx > 2))
        return -EINVAL;
    const uint8_t *set = r->ecx ? user(p, r->ecx, 8, false) : 0;
    uint8_t *old = r->edx ? user(p, r->edx, 8, true) : 0;
    if ((r->ecx && !set) || (r->edx && !old))
        return -EFAULT;
    uint32_t bits[2];
    if (set) {
        bits[0] = read32(set);
        bits[1] = read32(set + 4);
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (old)
            write32(old + i * 4, p->mask[i]);
        if (set)
            p->mask[i] = r->ebx == 0 ? p->mask[i] | bits[i] :
                         r->ebx == 1 ? p->mask[i] & ~bits[i] : bits[i];
    }
    p->mask[0] &= ~((1u << (SIGKILL - 1)) | (1u << (SIGSTOP - 1)));
    return 0;
}

static int signal_action(struct process *p, const struct native_frame *r)
{
    unsigned number = r->ebx;
    if (r->esi != 8 || !number || number >= 32 ||
        (r->ecx && (number == SIGKILL || number == SIGSTOP)))
        return -EINVAL;
    const uint8_t *set = r->ecx ? user(p, r->ecx, 20, false) : 0;
    uint8_t *old = r->edx ? user(p, r->edx, 20, true) : 0;
    if ((r->ecx && !set) || (r->edx && !old))
        return -EFAULT;
    struct signal_action *action = &p->actions[number];
    struct signal_action next;
    if (set) {
        copy((uint8_t *)&next, set, sizeof(next));
        if (next.flags & ~(SA_RESTORER | SA_RESTART | 1u))
            return -EINVAL;
        if (next.handler > 1 && (!(next.flags & SA_RESTORER) ||
            !user(p, next.handler, 1, false) || !user(p, next.restorer, 1, false)))
            return -EFAULT;
    }
    if (old)
        copy(old, (const uint8_t *)action, sizeof(*action));
    if (set)
        copy((uint8_t *)action, (const uint8_t *)&next, sizeof(next));
    return 0;
}

static int signal_send(struct process *p, int target, unsigned number)
{
    if (number >= 32 || number == SIGSTOP)
        return -EINVAL;
    if (target == INT32_MIN)
        return -ESRCH;
    bool found = false;
    for (unsigned i = 0; i < TASKS; ++i) {
        struct process *other = &runtime->tasks[i];
        if (other->state == FREE || other->state == ZOMBIE)
            continue;
        bool match = target > 0 ? other->pid == (unsigned)target :
                     target == -1 ? other != p && other->pid != 1 :
                     other->group == (target ? (unsigned)-target : p->group);
        if (!match)
            continue;
        found = true;
        if (number)
            other->pending |= 1u << (number - 1);
    }
    return found ? 0 : -ESRCH;
}

static unsigned pending_signal(struct process *p)
{
    uint32_t allowed = p->pending & ~p->mask[0];
    if (allowed & (1u << (SIGKILL - 1)))
        return SIGKILL;
    if (p->in_signal)
        return 0;
    for (unsigned i = 1; i < 32; ++i) {
        uint32_t bit = 1u << (i - 1);
        if (!(allowed & bit))
            continue;
        unsigned handler = p->actions[i].handler;
        if (handler == 1 || (!handler && (i == SIGCHLD || i == SIGCONT || i == 23 || i == 28))) {
            p->pending &= ~bit;
            continue;
        }
        return i;
    }
    return 0;
}

static void deliver_signal(struct process *p, unsigned number)
{
    struct signal_action *action = &p->actions[number];
    uint32_t bit = 1u << (number - 1);
    p->pending &= ~bit;
    if (!action->handler) {
        finish_task(p, number);
        return;
    }
    struct native_frame *frame = &p->space.frame;
    uint32_t stack = ((frame->esp - 8) & ~15u) - 4;
    uint8_t *out = user(p, stack, 8, true);
    if (!out) {
        finish_task(p, 11);
        return;
    }
    copy((uint8_t *)&p->saved_frame, (const uint8_t *)frame, sizeof(*frame));
    copy(p->saved_fpu, p->space.fpu, sizeof(p->saved_fpu));
    for (unsigned i = 0; i < 2; ++i) {
        p->saved_mask[i] = p->mask[i];
        p->mask[i] |= action->mask[i];
    }
    p->mask[0] = (p->mask[0] | bit) &
                 ~((1u << (SIGKILL - 1)) | (1u << (SIGSTOP - 1)));
    p->in_signal = true;
    write32(out, action->restorer);
    write32(out + 4, number);
    frame->esp = stack;
    frame->eip = action->handler;
}

static int signal_return(struct process *p)
{
    if (!p->in_signal)
        return -EINVAL;
    copy((uint8_t *)&p->space.frame, (const uint8_t *)&p->saved_frame, sizeof(p->saved_frame));
    copy(p->space.fpu, p->saved_fpu, sizeof(p->saved_fpu));
    p->mask[0] = p->saved_mask[0];
    p->mask[1] = p->saved_mask[1];
    p->in_signal = false;
    return RESUMED;
}

static int sleep_task(struct process *p, const struct native_frame *r)
{
    bool wide = r->eax == 407;
    uint32_t addr = r->eax == 162 ? r->ebx : r->edx;
    if (r->eax != 162 && (r->ebx > 1 || r->ecx))
        return -EINVAL;
    const uint8_t *req = user(p, addr, wide ? 16 : 8, false);
    if (!req)
        return -EFAULT;
    unsigned seconds = read32(req), nanos = read32(req + (wide ? 8 : 4));
    if (seconds > 2147482 || nanos >= 1000000000 || (wide && read32(req + 4)))
        return -EINVAL;
    return timeout(p, seconds * 1000 + (nanos + 999999) / 1000000) ? 0 : BLOCKED;
}

static int clock_time(struct process *p, const struct native_frame *r)
{
    bool timeval = r->eax == 78;
    if (!timeval && r->ebx != 0 && r->ebx != 1 && r->ebx != 4)
        return -EINVAL;
    bool realtime = timeval || !r->ebx;
    uint32_t base = realtime ? native_epoch() : 0;
    if (realtime && !base)
        return -EIO;
    bool wide = r->eax == 403;
    if (timeval && !r->ebx)
        return 0;
    uint8_t *time = user(p, timeval ? r->ebx : r->ecx, wide ? 16 : 8, true);
    if (!time)
        return -EFAULT;
    if (wide)
        zero(time, 16);
    uint32_t seconds = base + runtime->now / 1000;
    if (!wide && seconds > INT32_MAX)
        return -75;
    write32(time, seconds);
    write32(time + (wide ? 8 : 4), (runtime->now % 1000) * (timeval ? 1000 : 1000000));
    return 0;
}

static int local_socket_open(struct process *p, unsigned family, unsigned type, unsigned protocol)
{
    if (family != 1 || (type & ~(O_NONBLOCK | O_CLOEXEC)) != 1 || protocol)
        return -EAFNOSUPPORT;
    unsigned number = available_fd(p, 0), slot = available_file(), index = 0;
    while (index < PIPE_COUNT && runtime->sockets[index].live)
        ++index;
    if (number == FD_COUNT || slot == FILE_COUNT || index == PIPE_COUNT)
        return -EMFILE;
    struct local_socket *socket = &runtime->sockets[index];
    zero(socket, sizeof(*socket));
    socket->live = true;
    runtime->files[slot] = (struct file){index + 1, 0, 2 | (type & O_NONBLOCK), VFS_SOCKET, 1};
    p->files[number] = slot + 1;
    if (type & O_CLOEXEC)
        p->cloexec |= 1u << number;
    return number;
}

static int local_address(struct process *p, unsigned address, unsigned length, char name[64])
{
    if (length < 4 || length > 110)
        return -EINVAL;
    const uint8_t *value = user(p, address, length, false);
    if (!value)
        return -EFAULT;
    if (read16(value) != 1)
        return -EAFNOSUPPORT;
    if (value[2] != '/')
        return -EINVAL;
    unsigned size = 0;
    while (size + 2 < length && value[size + 2] && size < 63) {
        name[size] = value[size + 2];
        ++size;
    }
    if (size + 2 < length && value[size + 2])
        return -ENAMETOOLONG;
    name[size] = 0;
    return 0;
}

static int local_socket_call(struct process *p, unsigned op, const uint32_t args[4])
{
    if (op == 1)
        return local_socket_open(p, args[0], args[1], args[2]);
    struct file *file = fd(p, args[0]);
    if (!file)
        return -EBADF;
    if (file->volume != VFS_SOCKET)
        return -EINVAL;
    struct local_socket *socket = &runtime->sockets[file->inode - 1];
    if (op == 4) {
        if (!socket->name[0])
            return -EINVAL;
        socket->backlog = args[1] >= PIPE_COUNT ? PIPE_COUNT : args[1] ? args[1] : 1;
        return 0;
    }
    if (op == 5 || op == 18) {
        unsigned flags = op == 18 ? args[3] : 0;
        if (!socket->backlog || flags & ~(O_NONBLOCK | O_CLOEXEC))
            return -EINVAL;
        uint8_t *size = args[1] ? user(p, args[2], 4, true) : 0;
        if (args[1] && !size)
            return -EFAULT;
        unsigned available = size ? read32(size) : 0;
        unsigned width = available < 2 ? available : 2;
        uint8_t *address = width ? user(p, args[1], width, true) : 0;
        if (width && !address)
            return -EFAULT;
        if (!socket->count)
            return file->flags & O_NONBLOCK ? -EAGAIN : BLOCKED;
        unsigned number = available_fd(p, 0);
        if (number == FD_COUNT)
            return -EMFILE;
        unsigned id = socket->accepted[0];
        --socket->count;
        for (unsigned i = 0; i < socket->count; ++i)
            socket->accepted[i] = socket->accepted[i + 1];
        p->files[number] = id;
        runtime->files[id - 1].flags |= flags & O_NONBLOCK;
        if (flags & O_CLOEXEC)
            p->cloexec |= 1u << number;
        if (width)
            address[0] = 1;
        if (width == 2)
            address[1] = 0;
        if (size)
            write32(size, 2);
        return number;
    }
    if ((op != 2 && op != 3) || socket->name[0])
        return -EINVAL;
    char name[64];
    int err = local_address(p, args[1], args[2], name);
    if (err)
        return err;
    struct local_socket *peer = 0;
    for (unsigned i = 0; i < PIPE_COUNT; ++i) {
        struct local_socket *candidate = &runtime->sockets[i];
        if (candidate->live && same(candidate->name, name)) {
            peer = candidate;
            break;
        }
    }
    if (op == 2) {
        if (peer)
            return -EADDRINUSE;
        for (unsigned i = 0; (socket->name[i] = name[i]); ++i)
            ;
        return 0;
    }
    if (!peer || !peer->backlog)
        return -ECONNREFUSED;
    if (peer->count >= peer->backlog)
        return -EAGAIN;
    unsigned slot = available_file();
    struct pipe *pipe = free_pipe();
    if (!pipe || slot == FILE_COUNT)
        return -ENOSPC;
    unsigned inode = pipe - runtime->pipes + 1;
    runtime->files[slot] = (struct file){inode, 0, 2, VFS_PIPE, 1};
    pipe->ends[0] = slot + 1;
    pipe->ends[1] = p->files[args[0]];
    pipe->connected = true;
    peer->accepted[peer->count++] = slot + 1;
    socket->live = false;
    file->inode = inode;
    file->position = 1;
    file->volume = VFS_PIPE;
    return 0;
}

static int legacy_socket_call(struct process *p, unsigned op, unsigned address)
{
    if (op < 1 || (op > 5 && op != 18))
        return -ENOSYS;
    unsigned count = op == 4 ? 2 : op == 18 ? 4 : 3;
    const uint8_t *input = user(p, address, count * 4, false);
    if (!input)
        return -EFAULT;
    uint32_t args[4];
    for (unsigned i = 0; i < count; ++i)
        args[i] = read32(input + i * 4);
    return local_socket_call(p, op, args);
}

static int framebuffer_ioctl(struct process *p, const struct native_frame *r)
{
    struct file *file = fd(p, r->ebx);
    if (!file)
        return -EBADF;
    const struct native_fb *fb = native_framebuffer();
    if (file->volume != VFS_DEV || file->inode != 4 || !fb)
        return -ENOTTY;
    unsigned size = r->ecx == 0x4600 ? 160 : r->ecx == 0x4602 ? 68 : 0;
    if (!size)
        return -ENOTTY;
    uint8_t *out = user(p, r->edx, size, true);
    if (!out)
        return -EFAULT;
    zero(out, size);
    if (r->ecx == 0x4602) {
        copy(out, (const uint8_t *)"DDOS GOP", 8);
        write32(out + 20, fb->size);
        write32(out + 32, 2); /* FB_VISUAL_TRUECOLOR */
        write32(out + 44, fb->stride * 4);
    } else {
        write32(out, fb->width);
        write32(out + 4, fb->height);
        write32(out + 8, fb->width);
        write32(out + 12, fb->height);
        write32(out + 24, 32);
        write32(out + 32, fb->red);
        write32(out + 36, 8);
        write32(out + 44, 8);
        write32(out + 48, 8);
        write32(out + 56, fb->blue);
        write32(out + 60, 8);
        write32(out + 88, UINT32_MAX);
        write32(out + 92, UINT32_MAX);
    }
    return 0;
}

static int syscall(struct process *p)
{
    struct native_frame *r = &p->space.frame;
    switch (r->eax) {
    case 2: return fork_task(p);
    case 3: return io_read(p, r->ebx, r->ecx, r->edx);
    case 4: return io_write(p, r->ebx, r->ecx, r->edx);
    case 5: return openat(p, AT_FDCWD, r->ebx, r->ecx);
    case 6:
        if (!fd(p, r->ebx))
            return -EBADF;
        close_fd(p, r->ebx);
        return 0;
    case 11: {
        char path[256];
        int error = copy_path(p, r->ebx, path);
        if (error)
            return error;
        return exec_image(p, path, r->ecx, r->edx);
    }
    case 12: case 195: case 196: case 300: return by_path(p, r);
    case 37: return signal_send(p, r->ebx, r->ecx);
    case 119: return signal_return(p);
    case 174: return signal_action(p, r);
    case 175: return signal_mask(p, r);
    case 238: return signal_send(p, r->ebx, r->ecx);
    case 270:
        return r->ebx == r->ecx ? signal_send(p, r->ecx, r->edx) : -ESRCH;
    case 65: return p->group;
    case 147: return p->session;
    case 57:
        if (r->ebx && r->ebx != p->pid)
            return -ESRCH;
        p->group = r->ecx ? r->ecx : p->pid;
        return 0;
    case 66:
        if (p->group == p->pid)
            return -1;
        return p->session = p->group = p->pid;
    case 20: case 224: return p->pid;
    case 64: return p->parent;
    case 114: return wait_child(p);
    case 39: case 296: return make_directory(p, r);
    case 45: return set_brk(p, r->ebx);
    case 42: return make_pipe(p, r->ebx, 0);
    case 331: return make_pipe(p, r->ebx, r->ecx);
    case 41: {
        unsigned to = available_fd(p, 0);
        return to == FD_COUNT ? -EMFILE : duplicate_fd(p, r->ebx, to, false);
    }
    case 63: return duplicate_fd(p, r->ebx, r->ecx, false);
    case 54: return framebuffer_ioctl(p, r);
    case 55: case 221: return file_control(p, r);
    case 91: {
        uint32_t address = r->ebx, length = r->ecx;
        if (p->space.fb_access && address == p->space.base + NATIVE_FB_OFFSET) {
            if (length != native_framebuffer()->size)
                return -EINVAL;
            p->space.fb_access = 0;
            return 0;
        }
        if (address < p->mmap_start || address >= p->mmap_end || address % NATIVE_PAGE ||
            !length || length > p->mmap_end - address)
            return -EINVAL;
        for (unsigned off = 0; off < length; off += NATIVE_PAGE)
            native_page(&p->space, (address - p->space.base + off) / NATIVE_PAGE, 0);
        return 0;
    }
    case 102: return legacy_socket_call(p, r->ebx, r->ecx);
    case 359: case 361: case 362: case 363: case 364: {
        unsigned op = r->eax == 359 ? 1 : r->eax == 364 ? 18 : r->eax - 359;
        uint32_t args[] = {r->ebx, r->ecx, r->edx, r->esi};
        return local_socket_call(p, op, args);
    }
    case 140: return seek(p, r);
    case 142: return select_files(p, r);
    case 168: return poll_files(p, r);
    case 162: case 267: case 407: return sleep_task(p, r);
    case 78: case 265: case 403: return clock_time(p, r);
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
        if (!file)
            return -EBADF;
        if (file->volume == VFS_B || file->volume == VFS_D) {
            vfs_select(file->volume);
            if (!ext2_get_inode(file->inode, &info))
                return -EIO;
            return store_stat(p, r->ecx, file->volume, &info);
        }
        enum volume volume = file->volume;
        info.number = file->inode;
        info.kind = volume == VFS_TMP ? EXT2_REGULAR : (volume == VFS_PIPE ? 0010000 : volume == VFS_SOCKET ? 0140000 : 0020000);
        info.mode = info.kind | (volume == VFS_TMP ? 0644 : 0666);
        info.size = volume == VFS_TMP ? runtime->tmp_size : 0;
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
        uint32_t offset = base - p->space.base;
        uint32_t max = p->space.size - 1 - offset;
        p->space.tls_base = offset;
        p->space.tls_limit = limit < max ? limit : max;
        p->tls_set = true;
        write32(tls, 11);
        return 0;
    }
    case 258:
        if (r->ebx && !user(p, r->ebx, 4, true))
            return -EFAULT;
        p->tid_address = r->ebx;
        return p->pid;
    case 295: return openat(p, r->ebx, r->ecx, r->edx);
    default: return -ENOSYS;
    }
}

static void dispatch(struct process *p)
{
    int result = syscall(p);
    if (result == BLOCKED) {
        p->state = WAIT_IO;
        return;
    }
    p->deadline_set = false;
    if (result != RESUMED && p->state != WAIT_CHILD)
        p->space.frame.eax = result;
    if (p->state == WAIT_IO)
        p->state = RUNNABLE;
}

static int desktop_task(struct process *server, unsigned slot, const char *path)
{
    struct process *child = &runtime->tasks[slot];
    child->cwd = server->cwd;
    child->cwd_volume = server->cwd_volume;
    child->pid = runtime->next_pid++;
    child->parent = server->pid;
    child->session = server->session;
    child->group = server->group;
    for (unsigned i = 0; i < 3; ++i) {
        struct file *file = fd(server, i);
        if (file) {
            ++file->refs;
            child->files[i] = server->files[i];
        }
    }
    native_space_init(&child->space, slot);
    int error = exec_image(child, path, 0, 0);
    if (!error)
        child->state = RUNNABLE;
    else {
        for (unsigned i = 0; i < 3; ++i)
            close_fd(child, i);
    }
    return error;
}

static void native_session(uint32_t directory, const char *path, bool quiet, bool desktop)
{
    runtime = native_init();
    if (!runtime) {
        if (!quiet)
            print("Native runtime unavailable\r\n");
        return;
    }
    zero(runtime, sizeof(*runtime));
    struct process *initial = &runtime->tasks[0];
    initial->cwd = directory;
    initial->cwd_volume = VFS_D;
    initial->session = initial->group = initial->pid = 2;
    initial->state = RUNNABLE;
    runtime->next_pid = 3;
    for (unsigned i = 0; i < 3; ++i) {
        runtime->files[i] = (struct file){3, 0, i != 0, VFS_DEV, 1};
        initial->files[i] = i + 1;
    }
    native_space_init(&initial->space, 0);
    int status = exec_image(initial, path, 0, 0);
    bool launch_manager = desktop && !status;
    if (desktop && status == -ENOENT) {
        path = "/bin/nanowm";
        status = exec_image(initial, path, 0, 0);
    }
    if (status) {
        if (!quiet || (status != -ENOENT && status != -ENODEV)) {
            print("Cannot load ");
            print(path);
            print("\r\n");
        }
        return;
    }

    native_clock_start();
    unsigned slot = 0;
    while (initial->state != ZOMBIE) {
        if (desktop && (platform_try_getc() & 0xff) == 27)
            return;
        if (launch_manager) {
            for (unsigned i = 0; i < PIPE_COUNT; ++i) {
                const struct local_socket *listener = &runtime->sockets[i];
                if (!listener->live || !listener->backlog || !same(listener->name, "/tmp/.nano-X"))
                    continue;
                if (desktop_task(initial, 1, "/bin/nanowm")) {
                    print("Cannot load /bin/nanowm\r\n");
                    return;
                }
                int bar = desktop_task(initial, 2, "/bin/taskbar");
                if (bar && bar != -ENOENT)
                    print("Cannot load /bin/taskbar\r\n");
                launch_manager = false;
                break;
            }
        }
        if (desktop && runtime->tasks[1].state == ZOMBIE)
            return;
        runtime->now = native_millis();
        bool waiting = false;
        for (unsigned i = 0; i < TASKS; ++i) {
            struct process *p = &runtime->tasks[i];
            if (p->state == WAIT_IO || p->state == WAIT_CHILD) {
                unsigned number = pending_signal(p);
                if (number) {
                    struct native_frame *frame = &p->space.frame;
                    unsigned call = frame->eax;
                    const uint8_t *instruction = user(p, frame->eip - 2, 2, false);
                    if ((p->actions[number].flags & SA_RESTART) &&
                        (call == 3 || call == 4 || call == 114 || call == 146) &&
                        instruction && read16(instruction) == 0x80cd)
                        frame->eip -= 2;
                    else
                        frame->eax = -EINTR;
                    p->state = RUNNABLE;
                    p->deadline_set = false;
                }
            }
            if (p->state == WAIT_IO) {
                dispatch(p);
                waiting |= p->state == WAIT_IO;
            }
        }
        unsigned scan = 0;
        while (scan < TASKS && runtime->tasks[slot].state != RUNNABLE) {
            slot = (slot + 1) % TASKS;
            ++scan;
        }
        if (scan == TASKS) {
            if (waiting)
                continue;
            break;
        }
        struct process *p = &runtime->tasks[slot];
        unsigned signal = pending_signal(p);
        if (signal) {
            deliver_signal(p, signal);
            if (p->state != RUNNABLE) {
                slot = (slot + 1) % TASKS;
                continue;
            }
        }
        struct native_frame *frame = &p->space.frame;
        uint32_t event = native_step(&p->space);
        if (event != NATIVE_TRAP) {
            if (!slot) {
                print("Native program fault ");
                number(event & 0xff);
                print("\r\n");
            }
            finish_task(p, 11);
        } else if (frame->eax == 1 || frame->eax == 252) {
            if (!slot) {
                print("Native exit ");
                number(frame->ebx & 0xff);
                print("\r\n");
            }
            finish_task(p, (frame->ebx & 0xff) << 8);
        } else {
            dispatch(p);
        }
        slot = (slot + 1) % TASKS;
    }
}

void native_start(uint32_t directory, const char *path, bool quiet)
{
    native_session(directory, path, quiet, false);
}

void native_desktop(uint32_t directory)
{
    if (!native_framebuffer()) {
        print("NanoWM needs UEFI GOP. Boot with python boot.py --uefi.\r\n");
        return;
    }
    print("Starting NanoWM. Press Esc to return to DDOS.\r\n");
    native_session(directory, "/bin/nano-X", false, true);
    platform_clear();
}

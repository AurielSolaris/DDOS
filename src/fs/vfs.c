#include "vfs.h"

#include "ext2.h"
#include "fat12.h"

static bool boot_mounted;

void vfs_mount(void)
{
    boot_mounted = fat12_mount();
    ext2_select(1);
    ext2_mount();
    ext2_select(0);
    ext2_mount();
}

bool vfs_select(enum volume volume)
{
    if (volume == VFS_A)
        return boot_mounted;
    if (volume == VFS_B || volume == VFS_D) {
        ext2_select(volume == VFS_B ? 1 : 0);
        return ext2_mounted();
    }
    return volume == VFS_TMP || volume == VFS_DEV;
}

static bool prefix(const char *path, const char *name)
{
    while (*name && *path == *name) {
        ++path;
        ++name;
    }
    return !*name && (!*path || *path == '/');
}

struct vfs_path vfs_resolve(const char *path, enum volume current, bool posix)
{
    if (*path && path[1] == ':') {
        unsigned drive = (unsigned char)path[0] & ~32u;
        return (struct vfs_path){drive >= 'A' && drive <= 'D' ? drive - 'A' : VFS_INVALID,
                                 path + 2};
    }
    if (posix && *path == '/') {
        if (prefix(path, "/dev"))
            return (struct vfs_path){VFS_DEV, path + 4};
        if (prefix(path, "/tmp"))
            return (struct vfs_path){VFS_TMP, path + 4};
        return (struct vfs_path){VFS_B, path};
    }
    return (struct vfs_path){current, path};
}
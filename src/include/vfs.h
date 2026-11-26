#ifndef DDOS_VFS_H
#define DDOS_VFS_H

#include <stdbool.h>

enum volume { VFS_A, VFS_B, VFS_C, VFS_D, VFS_DEV, VFS_TMP, VFS_PIPE, VFS_SOCKET, VFS_INVALID };

struct vfs_path {
    enum volume volume;
    const char *name;
};

void vfs_mount(void);
bool vfs_select(enum volume);
struct vfs_path vfs_resolve(const char *path, enum volume current, bool posix);

#endif
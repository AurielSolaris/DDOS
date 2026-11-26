#include "vfs.h"

#include "ata.h"
#include "ext2.h"
#include "ehci.h"
#include "fat12.h"

static bool boot_mounted;
static struct block_device ata_disks[2], usb_disk;

void vfs_mount(void)
{
    boot_mounted = fat12_mount();
    for (unsigned unit = 0; unit < 2; ++unit) {
        ext2_select(unit);
        ata_block_device(unit, &ata_disks[unit]);
        ext2_mount(&ata_disks[unit]);
    }
    if (usb_storage_device(&usb_disk)) {
        ext2_select(2);
        ext2_mount(&usb_disk);
    }
    ext2_select(0);
}

bool vfs_select(enum volume volume)
{
    if (volume == VFS_A)
        return boot_mounted;
    if (vfs_ext2(volume)) {
        ext2_select(volume == VFS_E ? 2 : volume == VFS_B ? 1 : 0);
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
        return (struct vfs_path){drive >= 'A' && drive <= 'E' ? drive - 'A' : VFS_INVALID,
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
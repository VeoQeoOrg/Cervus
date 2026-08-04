#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/syscall/errno.h"
#include "../../../include/fs/vfs.h"
#include <stdint.h>
#include <string.h>

extern vnode_t *ninep_mount(uint32_t ip, uint16_t port);
extern int vfs_set_mount_info(const char *path, const char *device, const char *fstype);

int64_t sys_mount9(uint64_t ip, uint64_t port, uint64_t path_ptr) {
    if (!path_ptr) return -EINVAL;
    if (!syscall_uptr_validate((void *)path_ptr, 256)) return -EFAULT;
    char path[256];
    if (syscall_copy_from_user(path, (void *)path_ptr, 256) < 0) return -EFAULT;
    path[255] = 0;
    if (!path[0]) return -EINVAL;

    vnode_t *root = ninep_mount((uint32_t)ip, (uint16_t)port);
    if (!root) return -EIO;
    int r = vfs_mount_fs(path, root, NULL, NULL, NULL);
    if (r < 0) { if (root->ops && root->ops->unref) root->ops->unref(root); return r; }
    vfs_set_mount_info(path, "9p", "9p");
    return 0;
}

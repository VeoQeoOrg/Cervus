#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"
#include <string.h>

int64_t sys_unlink(uint64_t path_ptr, uint64_t a2, uint64_t a3,
                   uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    char path[256];
    if (syscall_strncpy_from_user(path, (const char *)path_ptr, sizeof(path)) < 0) return -EFAULT;
    char dirpath[256];
    strncpy(dirpath, path, 255);
    char *slash = NULL;
    for (int i = (int)strlen(dirpath) - 1; i >= 0; i--) {
        if (dirpath[i] == '/') { slash = &dirpath[i]; break; }
    }
    if (!slash) return -EINVAL;
    char name[256];
    strncpy(name, slash + 1, 255);
    if (slash == dirpath) dirpath[1] = '\0';
    else                  *slash = '\0';
    vnode_t *dir = NULL;
    int r = vfs_lookup(dirpath, &dir);
    if (r < 0) return r;
    if (!dir->ops || !dir->ops->unlink) { vnode_unref(dir); return -ENOSYS; }
    r = dir->ops->unlink(dir, name);
    vnode_unref(dir);
    if (r == 0) vfs_sync_all();
    return r;
}

int64_t sys_rmdir(uint64_t p, uint64_t a2, uint64_t a3,
                  uint64_t a4, uint64_t a5, uint64_t a6)
{
    return sys_unlink(p, a2, a3, a4, a5, a6);
}

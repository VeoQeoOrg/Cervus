#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"

int64_t sys_statvfs(uint64_t a1, uint64_t a2, uint64_t a3,
                    uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    const char *path = (const char *)a1;
    vfs_statvfs_t *out = (vfs_statvfs_t *)a2;
    if (!path || !out) return -EINVAL;
    return vfs_statvfs(path, out);
}

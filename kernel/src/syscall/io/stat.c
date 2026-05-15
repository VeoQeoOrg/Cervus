#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"

int64_t sys_stat(uint64_t path_ptr, uint64_t stat_ptr)
{
    if (!stat_ptr) return -EINVAL;
    char kpath[VFS_MAX_PATH];
    if (syscall_strncpy_from_user(kpath, (const char *)path_ptr, sizeof(kpath)) < 0) return -EFAULT;
    vfs_stat_t st;
    int r = vfs_stat(kpath, &st);
    if (r < 0) return (int64_t)r;
    return syscall_copy_to_user((void *)stat_ptr, &st, sizeof(st));
}

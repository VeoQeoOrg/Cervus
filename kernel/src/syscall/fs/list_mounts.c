#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"

int64_t sys_list_mounts(uint64_t a1, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!a1 || (int64_t)a2 <= 0) return -EINVAL;
    int max = (int)a2;
    if (max > VFS_MAX_MOUNTS) max = VFS_MAX_MOUNTS;

    if (!syscall_uptr_validate((void *)a1, (size_t)max * sizeof(vfs_mount_info_t)))
        return -EFAULT;

    vfs_mount_info_t kbuf[VFS_MAX_MOUNTS];
    int n = vfs_list_mounts(kbuf, max);
    if (n < 0) return n;
    if (syscall_copy_to_user((void *)a1, kbuf, (size_t)n * sizeof(vfs_mount_info_t)) < 0)
        return -EFAULT;
    return n;
}

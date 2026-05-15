#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"

int64_t sys_list_mounts(uint64_t a1, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    vfs_mount_info_t *out = (vfs_mount_info_t *)a1;
    int max = (int)a2;
    if (!out || max <= 0) return -EINVAL;
    return vfs_list_mounts(out, max);
}

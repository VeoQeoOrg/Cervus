#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"

int64_t sys_mkdir(uint64_t path_ptr, uint64_t mode, uint64_t a3,
                  uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    char path[256];
    if (syscall_strncpy_from_user(path, (const char *)path_ptr, sizeof(path)) < 0) return -EFAULT;
    int r = vfs_mkdir(path, (uint32_t)mode);
    if (r == 0) vfs_sync_all();
    return r;
}

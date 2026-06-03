#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"

int64_t sys_statvfs(uint64_t a1, uint64_t a2, uint64_t a3,
                    uint64_t a4, uint64_t a5, uint64_t a6)
{
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (!a1 || !a2) return -EINVAL;
    if (!syscall_uptr_validate((void *)a2, sizeof(vfs_statvfs_t))) return -EFAULT;
    char kpath[VFS_MAX_PATH];
    int rp = syscall_resolve_path_from_user(kpath, (const char *)a1, sizeof(kpath));
    if (rp < 0) return rp;
    vfs_statvfs_t kout;
    int r = vfs_statvfs(kpath, &kout);
    if (r < 0) return r;
    return syscall_copy_to_user((void *)a2, &kout, sizeof(kout));
}

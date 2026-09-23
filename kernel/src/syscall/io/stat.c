#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"

int64_t sys_stat(uint64_t path_ptr, uint64_t stat_ptr)
{
    if (!stat_ptr) return -EINVAL;
    char kpath[VFS_MAX_PATH];
    int rp = syscall_resolve_path_from_user(kpath, (const char *)path_ptr, sizeof(kpath));
    if (rp < 0) return rp;
    vfs_stat_t st;
    int r = vfs_stat(kpath, &st);
    if (r < 0) return (int64_t)r;
    return syscall_copy_to_user((void *)stat_ptr, &st, VFS_STAT_V1_SIZE);
}

int64_t sys_lstat(uint64_t path_ptr, uint64_t stat_ptr)
{
    if (!stat_ptr) return -EINVAL;
    char kpath[VFS_MAX_PATH];
    int rp = syscall_resolve_path_from_user(kpath, (const char *)path_ptr, sizeof(kpath));
    if (rp < 0) return rp;
    vfs_stat_t st;
    int r = vfs_lstat(kpath, &st);
    if (r < 0) return (int64_t)r;
    return syscall_copy_to_user((void *)stat_ptr, &st, VFS_STAT_V1_SIZE);
}

int64_t sys_kstat(uint64_t kind, uint64_t arg, uint64_t stat_ptr)
{
    if (!stat_ptr) return -EINVAL;
    vfs_stat_t st;
    int r;
    if (kind == 2) {
        task_t *t = syscall_cur_task();
        if (!t || !t->fd_table) return -EBADF;
        vfs_file_t *f = fd_get(t->fd_table, (int)arg);
        if (!f) return -EBADF;
        r = vfs_fstat(f, &st);
        fd_put(f);
    } else if (kind <= 1) {
        char kpath[VFS_MAX_PATH];
        int rp = syscall_resolve_path_from_user(kpath, (const char *)arg, sizeof(kpath));
        if (rp < 0) return rp;
        r = kind ? vfs_lstat(kpath, &st) : vfs_stat(kpath, &st);
    } else {
        return -EINVAL;
    }
    if (r < 0) return (int64_t)r;
    return syscall_copy_to_user((void *)stat_ptr, &st, sizeof(st));
}

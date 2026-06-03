#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"

int64_t sys_truncate(uint64_t path_ptr, uint64_t length)
{
    char kpath[VFS_MAX_PATH];
    int rp = syscall_resolve_path_from_user(kpath, (const char *)path_ptr, sizeof(kpath));
    if (rp < 0) return rp;
    return vfs_truncate(kpath, length);
}

int64_t sys_ftruncate(uint64_t fd, uint64_t length)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;
    int r = vfs_ftruncate(f, length);
    fd_put(f);
    return r;
}

int64_t sys_fsync(uint64_t fd)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;
    int r = vfs_fsync(f);
    fd_put(f);
    return r;
}

int64_t sys_fdatasync(uint64_t fd)
{
    return sys_fsync(fd);
}

#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"

int64_t sys_readdir(uint64_t fd, uint64_t dirent_ptr)
{
    if (!dirent_ptr) return -EINVAL;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;
    vfs_dirent_t kd;
    int r = vfs_readdir(f, &kd);
    if (r < 0) return (int64_t)r;
    return syscall_copy_to_user((void *)dirent_ptr, &kd, sizeof(kd));
}

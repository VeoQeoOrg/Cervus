#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"

int64_t sys_fstat(uint64_t fd, uint64_t stat_ptr)
{
    if (!stat_ptr) return -EINVAL;
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    vfs_file_t *f = fd_get(t->fd_table, (int)fd);
    if (!f) return -EBADF;
    vfs_stat_t st;
    int r = vfs_fstat(f, &st);
    if (r < 0) return (int64_t)r;
    return syscall_copy_to_user((void *)stat_ptr, &st, sizeof(st));
}

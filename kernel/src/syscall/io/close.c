#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"

int64_t sys_close(uint64_t fd)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    return (int64_t)fd_close(t->fd_table, (int)fd);
}

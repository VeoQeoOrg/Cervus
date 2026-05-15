#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"

int64_t sys_dup2(uint64_t oldfd, uint64_t newfd)
{
    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    int r = fd_dup2(t->fd_table, (int)oldfd, (int)newfd);
    return r < 0 ? (int64_t)r : (int64_t)newfd;
}

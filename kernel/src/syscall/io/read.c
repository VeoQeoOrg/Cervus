#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"
#include <string.h>

int64_t sys_read(uint64_t fd, uint64_t buf_ptr, uint64_t count)
{
    if (count == 0) return 0;
    if (count > 65536) count = 65536;

    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (!syscall_uptr_validate((void *)buf_ptr, count)) return -EFAULT;

    vfs_file_t *file = NULL;
    if (t->fd_table) file = fd_get(t->fd_table, (int)fd);
    if (!file) return -EBADF;

    char kbuf[4096];
    size_t total = 0;
    while (total < count) {
        size_t chunk = count - total;
        if (chunk > sizeof(kbuf)) chunk = sizeof(kbuf);
        int64_t r = vfs_read(file, kbuf, chunk);
        if (r < 0) return total ? (int64_t)total : r;
        if (r == 0) break;
        memcpy((char *)buf_ptr + total, kbuf, (size_t)r);
        total += (size_t)r;
        if ((size_t)r < chunk) break;
    }
    return (int64_t)total;
}

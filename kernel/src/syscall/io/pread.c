#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"
#include <string.h>

int64_t sys_pread(uint64_t fd, uint64_t buf_ptr, uint64_t count, uint64_t offset)
{
    if (count == 0) return 0;
    if (count > 65536) count = 65536;

    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    if (!syscall_uptr_validate((void *)buf_ptr, count)) return -EFAULT;

    vfs_file_t *file = fd_get(t->fd_table, (int)fd);
    if (!file) return -EBADF;

    char kbuf[4096];
    size_t total = 0;
    int64_t err = 0;
    while (total < count) {
        size_t chunk = count - total;
        if (chunk > sizeof(kbuf)) chunk = sizeof(kbuf);
        int64_t r = vfs_pread(file, kbuf, chunk, offset + total);
        if (r < 0) { err = r; break; }
        if (r == 0) break;
        memcpy((char *)buf_ptr + total, kbuf, (size_t)r);
        total += (size_t)r;
        if ((size_t)r < chunk) break;
    }
    fd_put(file);
    if (total == 0 && err < 0) return err;
    return (int64_t)total;
}

int64_t sys_pwrite(uint64_t fd, uint64_t buf_ptr, uint64_t count, uint64_t offset)
{
    if (count == 0) return 0;
    if (count > 65536) count = 65536;

    task_t *t = syscall_cur_task();
    if (!t || !t->fd_table) return -EBADF;
    if (!syscall_uptr_validate((void *)buf_ptr, count)) return -EFAULT;

    vfs_file_t *file = fd_get(t->fd_table, (int)fd);
    if (!file) return -EBADF;

    char kbuf[4096];
    size_t total = 0;
    int64_t err = 0;
    while (total < count) {
        size_t chunk = count - total;
        if (chunk > sizeof(kbuf)) chunk = sizeof(kbuf);
        memcpy(kbuf, (const char *)buf_ptr + total, chunk);
        int64_t w = vfs_pwrite(file, kbuf, chunk, offset + total);
        if (w < 0) { err = w; break; }
        if (w == 0) break;
        total += (size_t)w;
        if ((size_t)w < chunk) break;
    }
    fd_put(file);
    if (total == 0 && err < 0) return err;
    return (int64_t)total;
}

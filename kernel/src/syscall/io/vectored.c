#include "../../../include/syscall/syscall_internal.h"
#include "../../../include/fs/vfs.h"
#include "../../../include/memory/pmm.h"
#include <string.h>

#define UIO_MAXIOV 1024

typedef struct {
    uint64_t base;
    uint64_t len;
} k_iovec_t;

static int64_t load_iov(uint64_t iov_ptr, uint64_t cnt, k_iovec_t **out, size_t *total)
{
    *out = NULL;
    *total = 0;
    if (cnt > UIO_MAXIOV) return -EINVAL;
    if (cnt == 0) return 0;
    k_iovec_t *v = kmalloc(cnt * sizeof(k_iovec_t));
    if (!v) return -ENOMEM;
    if (syscall_copy_from_user(v, (const void *)iov_ptr, cnt * sizeof(k_iovec_t)) < 0) {
        kfree(v);
        return -EFAULT;
    }
    size_t sum = 0;
    for (uint64_t i = 0; i < cnt; i++) {
        if (v[i].len > (1ULL << 62) || sum + v[i].len < sum) { kfree(v); return -EINVAL; }
        if (v[i].len && !syscall_uptr_validate((void *)v[i].base, v[i].len)) { kfree(v); return -EFAULT; }
        sum += v[i].len;
    }
    *out = v;
    *total = sum;
    return 0;
}

static size_t gather(const k_iovec_t *v, uint64_t cnt, size_t skip, char *dst, size_t want)
{
    size_t got = 0;
    for (uint64_t i = 0; i < cnt && got < want; i++) {
        if (skip >= v[i].len) { skip -= v[i].len; continue; }
        size_t n = v[i].len - skip;
        if (n > want - got) n = want - got;
        memcpy(dst + got, (const char *)v[i].base + skip, n);
        got += n;
        skip = 0;
    }
    return got;
}

static void scatter(const k_iovec_t *v, uint64_t cnt, size_t skip, const char *src, size_t len)
{
    size_t done = 0;
    for (uint64_t i = 0; i < cnt && done < len; i++) {
        if (skip >= v[i].len) { skip -= v[i].len; continue; }
        size_t n = v[i].len - skip;
        if (n > len - done) n = len - done;
        memcpy((char *)v[i].base + skip, src + done, n);
        done += n;
        skip = 0;
    }
}

static int64_t do_vec(uint64_t fd, uint64_t iov_ptr, uint64_t cnt, int64_t off, bool writing)
{
    task_t *t = syscall_cur_task();
    if (!t) return -ESRCH;
    if (off < -1) return -EINVAL;
    k_iovec_t *v;
    size_t total;
    int64_t r = load_iov(iov_ptr, cnt, &v, &total);
    if (r < 0) return r;
    vfs_file_t *file = t->fd_table ? fd_get(t->fd_table, (int)fd) : NULL;
    if (!file) { kfree(v); return -EBADF; }
    if (total > (16u << 20)) total = 16u << 20;

    char kbuf[4096];
    size_t cap = 0;
    char *bb = total ? syscall_bounce_get(total, &cap, kbuf, sizeof(kbuf)) : kbuf;
    size_t done = 0;
    int64_t err = 0;
    while (done < total) {
        size_t chunk = total - done;
        if (chunk > cap) chunk = cap;
        int64_t n;
        if (writing) {
            gather(v, cnt, done, bb, chunk);
            n = off < 0 ? vfs_write(file, bb, chunk) : vfs_pwrite(file, bb, chunk, (uint64_t)off + done);
        } else {
            n = off < 0 ? vfs_read(file, bb, chunk) : vfs_pread(file, bb, chunk, (uint64_t)off + done);
            if (n > 0) scatter(v, cnt, done, bb, (size_t)n);
        }
        if (n < 0) { err = n; break; }
        done += (size_t)n;
        if ((size_t)n < chunk) break;
    }
    syscall_bounce_put(bb, kbuf);
    if (writing && done) {
        int s = vfs_write_synced(file);
        if (s < 0) { done = 0; err = s; }
    }
    fd_put(file);
    kfree(v);
    if (done) return (int64_t)done;
    return err;
}

int64_t sys_readv(uint64_t fd, uint64_t iov, uint64_t cnt)
{
    return do_vec(fd, iov, cnt, -1, false);
}

int64_t sys_writev(uint64_t fd, uint64_t iov, uint64_t cnt)
{
    return do_vec(fd, iov, cnt, -1, true);
}

int64_t sys_preadv(uint64_t fd, uint64_t iov, uint64_t cnt, uint64_t off)
{
    if ((int64_t)off < 0) return -EINVAL;
    return do_vec(fd, iov, cnt, (int64_t)off, false);
}

int64_t sys_pwritev(uint64_t fd, uint64_t iov, uint64_t cnt, uint64_t off)
{
    if ((int64_t)off < 0) return -EINVAL;
    return do_vec(fd, iov, cnt, (int64_t)off, true);
}

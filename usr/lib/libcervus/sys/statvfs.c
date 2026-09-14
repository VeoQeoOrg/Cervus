#include <sys/statvfs.h>
#include <sys/cervus.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>

static void fill(struct statvfs *buf, const cervus_statvfs_t *cv)
{
    memset(buf, 0, sizeof *buf);
    buf->f_bsize   = (unsigned long)cv->f_bsize;
    buf->f_frsize  = (unsigned long)cv->f_bsize;
    buf->f_blocks  = cv->f_blocks;
    buf->f_bfree   = cv->f_bfree;
    buf->f_bavail  = cv->f_bavail;
    buf->f_files   = cv->f_files;
    buf->f_ffree   = cv->f_ffree;
    buf->f_favail  = cv->f_ffree;
    buf->f_flag    = cv->f_flag;
    buf->f_namemax = cv->f_namemax;
}

int statvfs(const char *path, struct statvfs *buf)
{
    if (!path || !buf) { errno = EFAULT; return -1; }
    cervus_statvfs_t cv;
    if (cervus_statvfs(path, &cv) != 0) return -1;
    fill(buf, &cv);
    return 0;
}

int fstatvfs(int fd, struct statvfs *buf)
{
    char link[64], target[1024];
    snprintf(link, sizeof link, "/proc/self/fd/%d", fd);
    long n = readlink(link, target, sizeof target - 1);
    if (n <= 0) { errno = ENOSYS; return -1; }
    target[n] = '\0';
    return statvfs(target, buf);
}

#include <sys/stat.h>
#include <sys/syscall.h>
#include <libcervus.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>

static int64_t sec_of(const struct timespec *ts, int i)
{
    if (!ts) return -1;
    if (ts[i].tv_nsec == UTIME_NOW) return -1;
    if (ts[i].tv_nsec == UTIME_OMIT) return 0;
    return ts[i].tv_sec > 0 ? (int64_t)ts[i].tv_sec : 1;
}

int futimens(int fd, const struct timespec times[2])
{
    return (int)__cervus_sys_ret(syscall3(SYS_FUTIMES, fd, (uint64_t)sec_of(times, 0),
                                          (uint64_t)sec_of(times, 1)));
}

int utimensat(int dirfd, const char *path, const struct timespec times[2], int flags)
{
    (void)flags;
    if (dirfd != AT_FDCWD && path && path[0] != '/') { errno = ENOSYS; return -1; }
    return (int)__cervus_sys_ret(syscall3(SYS_UTIMES, path, (uint64_t)sec_of(times, 0),
                                          (uint64_t)sec_of(times, 1)));
}

int futimes(int fd, const struct timeval tv[2])
{
    if (!tv) return futimens(fd, NULL);
    struct timespec ts[2] = { { tv[0].tv_sec, tv[0].tv_usec * 1000 }, { tv[1].tv_sec, tv[1].tv_usec * 1000 } };
    return futimens(fd, ts);
}

int lutimes(const char *path, const struct timeval tv[2])
{
    if (!tv) return utimensat(AT_FDCWD, path, NULL, AT_SYMLINK_NOFOLLOW);
    struct timespec ts[2] = { { tv[0].tv_sec, tv[0].tv_usec * 1000 }, { tv[1].tv_sec, tv[1].tv_usec * 1000 } };
    return utimensat(AT_FDCWD, path, ts, AT_SYMLINK_NOFOLLOW);
}

int lchown(const char *path, uid_t owner, gid_t group)
{
    return (int)__cervus_sys_ret(syscall3(SYS_LCHOWN, path, owner, group));
}

int utimes_at(const char *path, int64_t atime, int64_t mtime)
{
    return (int)__cervus_sys_ret(syscall3(SYS_UTIMES, path,
                                          (uint64_t)atime, (uint64_t)mtime));
}

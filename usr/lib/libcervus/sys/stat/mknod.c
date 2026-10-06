#include <sys/stat.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <libcervus.h>

int mknodat(int dirfd, const char *path, mode_t mode, dev_t dev)
{
    return (int)__cervus_sys_ret(syscall4(SYS_MKNODAT, (uint64_t)(int64_t)dirfd, (uint64_t)path,
                                          (uint64_t)mode, (uint64_t)dev));
}

int mknod(const char *path, mode_t mode, dev_t dev)
{
    return mknodat(AT_FDCWD, path, mode, dev);
}

int mkfifoat(int dirfd, const char *path, mode_t mode)
{
    return mknodat(dirfd, path, (mode & 07777) | S_IFIFO, 0);
}

int mkfifo(const char *path, mode_t mode)
{
    return mknodat(AT_FDCWD, path, (mode & 07777) | S_IFIFO, 0);
}

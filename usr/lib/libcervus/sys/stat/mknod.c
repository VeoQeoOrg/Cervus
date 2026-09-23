#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <libcervus.h>

int mknod(const char *path, mode_t mode, dev_t dev)
{
    (void)dev;
    if (!path) {
        __cervus_errno = EFAULT;
        return -1;
    }
    switch (mode & S_IFMT) {
    case 0:
    case S_IFREG: {
        int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, mode & 07777);
        if (fd < 0) return -1;
        close(fd);
        return 0;
    }
    case S_IFIFO:
        __cervus_errno = ENOTSUP;
        return -1;
    default:
        __cervus_errno = EPERM;
        return -1;
    }
}

int mkfifo(const char *path, mode_t mode)
{
    return mknod(path, (mode & 07777) | S_IFIFO, 0);
}

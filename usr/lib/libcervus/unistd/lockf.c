#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <libcervus.h>

int lockf(int fd, int cmd, off_t len)
{
    struct flock fl = { .l_whence = SEEK_CUR, .l_start = 0, .l_len = len };
    switch (cmd) {
    case F_TEST:
        fl.l_type = F_RDLCK;
        if (fcntl(fd, F_GETLK, &fl) < 0) return -1;
        if (fl.l_type == F_UNLCK || fl.l_pid == getpid()) return 0;
        __cervus_set_errno(EACCES);
        return -1;
    case F_ULOCK:
        fl.l_type = F_UNLCK;
        return fcntl(fd, F_SETLK, &fl);
    case F_TLOCK:
        fl.l_type = F_WRLCK;
        return fcntl(fd, F_SETLK, &fl);
    case F_LOCK:
        fl.l_type = F_WRLCK;
        return fcntl(fd, F_SETLKW, &fl);
    }
    __cervus_set_errno(EINVAL);
    return -1;
}

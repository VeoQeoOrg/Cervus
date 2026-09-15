#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int flock(int fd, int operation)
{
    struct flock fl;
    memset(&fl, 0, sizeof fl);
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;

    switch (operation & ~LOCK_NB) {
        case LOCK_SH: fl.l_type = F_RDLCK; break;
        case LOCK_EX: fl.l_type = F_WRLCK; break;
        case LOCK_UN: fl.l_type = F_UNLCK; break;
        default: errno = EINVAL; return -1;
    }

    int cmd = (operation & LOCK_NB) ? F_SETLK : F_SETLKW;
    if (fcntl(fd, cmd, &fl) < 0) {
        if (errno == EAGAIN) errno = EWOULDBLOCK;
        return -1;
    }
    return 0;
}

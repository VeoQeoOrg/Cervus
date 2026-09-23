#include <sys/random.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <libcervus.h>

ssize_t getrandom(void *buf, size_t len, unsigned int flags)
{
    (void)flags;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t got = 0;
    while (got < len) {
        ssize_t r = read(fd, (char *)buf + got, len - got);
        if (r < 0) {
            if (__cervus_errno == EINTR) continue;
            close(fd);
            return got ? (ssize_t)got : -1;
        }
        if (r == 0) break;
        got += (size_t)r;
    }
    close(fd);
    return (ssize_t)got;
}

int getentropy(void *buf, size_t len)
{
    if (len > 256) {
        __cervus_errno = EIO;
        return -1;
    }
    return getrandom(buf, len, 0) == (ssize_t)len ? 0 : -1;
}

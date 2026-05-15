#include <unistd.h>
#include <errno.h>
#include <libcervus.h>

ssize_t readlink(const char *path, char *buf, size_t bufsiz)
{
    (void)path; (void)buf; (void)bufsiz;
    __cervus_errno = ENOSYS;
    return -1;
}

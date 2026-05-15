#include <unistd.h>
#include <errno.h>
#include <libcervus.h>

int fchdir(int fd)
{
    (void)fd;
    __cervus_errno = ENOSYS;
    return -1;
}

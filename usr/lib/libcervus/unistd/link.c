#include <unistd.h>
#include <errno.h>

int link(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath) { errno = EFAULT; return -1; }
    errno = EPERM;
    return -1;
}

#include <unistd.h>
#include <errno.h>
#include <libcervus.h>

int symlink(const char *target, const char *linkpath)
{
    (void)target; (void)linkpath;
    __cervus_errno = ENOSYS;
    return -1;
}

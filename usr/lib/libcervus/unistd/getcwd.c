#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/syscall.h>
#include <libcervus.h>

char *getcwd(char *buf, size_t size)
{
    if (!buf || size < 2) { __cervus_errno = EINVAL; return NULL; }
    int64_t r = __syscall6(SYS_GETCWD, (uint64_t)buf, (uint64_t)size, 0, 0, 0, 0);
    if (r < 0) { __cervus_errno = (int)-r; return NULL; }
    return buf;
}

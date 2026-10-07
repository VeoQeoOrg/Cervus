#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <sys/syscall.h>
#include <libcervus.h>

char *getcwd(char *buf, size_t size)
{
    char tmp[PATH_MAX];
    char *dst = buf;
    if (!buf) {
        dst = tmp;
        size = sizeof tmp;
    } else if (size == 0) {
        __cervus_set_errno(EINVAL);
        return NULL;
    }
    int64_t r = __syscall6(SYS_GETCWD, (uint64_t)dst, (uint64_t)size, 0, 0, 0, 0);
    if (r < 0) { __cervus_set_errno((int)-r); return NULL; }
    if (buf) return buf;
    char *copy = strdup(tmp);
    if (!copy) __cervus_set_errno(ENOMEM);
    return copy;
}

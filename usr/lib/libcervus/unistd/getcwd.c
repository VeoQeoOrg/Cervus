#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <libcervus.h>

char *getcwd(char *buf, size_t size)
{
    if (!buf || size < 2) { __cervus_errno = EINVAL; return NULL; }
    const char *c = __cervus_get_cwd();
    size_t n = strlen(c);
    if (n + 1 > size) { __cervus_errno = ERANGE; return NULL; }
    memcpy(buf, c, n + 1);
    return buf;
}

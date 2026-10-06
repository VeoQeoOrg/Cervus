#include <unistd.h>
#include <string.h>
#include <errno.h>

size_t confstr(int name, char *buf, size_t len)
{
    const char *v;
    switch (name) {
    case _CS_PATH:                       v = "/bin:/usr/bin"; break;
    case _CS_POSIX_V7_LP64_OFF64_CFLAGS:
    case _CS_POSIX_V7_LP64_OFF64_LDFLAGS:
    case _CS_POSIX_V7_LP64_OFF64_LIBS:   v = ""; break;
    default:
        errno = EINVAL;
        return 0;
    }
    size_t n = strlen(v) + 1;
    if (buf && len) {
        size_t c = n < len ? n : len;
        memcpy(buf, v, c - 1);
        buf[c - 1] = '\0';
    }
    return n;
}

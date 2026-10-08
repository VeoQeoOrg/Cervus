#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

int vdprintf(int fd, const char *fmt, va_list ap)
{
    char *buf = NULL;
    int n = vasprintf(&buf, fmt, ap);
    if (n < 0) return -1;
    size_t off = 0;
    while (off < (size_t)n) {
        ssize_t w = write(fd, buf + off, (size_t)n - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            free(buf);
            return -1;
        }
        off += (size_t)w;
    }
    free(buf);
    return n;
}

int dprintf(int fd, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vdprintf(fd, fmt, ap);
    va_end(ap);
    return n;
}

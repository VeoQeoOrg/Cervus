#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

int vasprintf(char **out, const char *fmt, va_list ap)
{
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (n < 0) return -1;

    char *buf = malloc((size_t)n + 1);
    if (!buf) return -1;

    int m = vsnprintf(buf, (size_t)n + 1, fmt, ap);
    if (m < 0) { free(buf); return -1; }

    *out = buf;
    return m;
}

int asprintf(char **out, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = vasprintf(out, fmt, ap);
    va_end(ap);
    return n;
}

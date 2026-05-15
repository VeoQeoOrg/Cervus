#include <stdio.h>
#include <stdarg.h>

extern int vfscanf(FILE *stream, const char *fmt, va_list ap);

int fscanf(FILE *stream, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vfscanf(stream, fmt, ap);
    va_end(ap);
    return n;
}

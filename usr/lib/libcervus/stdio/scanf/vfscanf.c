#include <stdio.h>
#include <stdarg.h>

int vfscanf(FILE *stream, const char *fmt, va_list ap)
{
    char buf[1024];
    if (!fgets(buf, (int)sizeof(buf), stream)) return EOF;
    return vsscanf(buf, fmt, ap);
}

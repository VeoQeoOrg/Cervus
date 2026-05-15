#include <stdio.h>
#include <stdarg.h>

int vscanf(const char *fmt, va_list ap)
{
    char buf[1024];
    if (!fgets(buf, (int)sizeof(buf), stdin)) return EOF;
    return vsscanf(buf, fmt, ap);
}

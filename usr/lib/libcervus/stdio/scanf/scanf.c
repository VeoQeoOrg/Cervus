#include <stdio.h>
#include <stdarg.h>

extern int vscanf(const char *fmt, va_list ap);

int scanf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vscanf(fmt, ap);
    va_end(ap);
    return n;
}

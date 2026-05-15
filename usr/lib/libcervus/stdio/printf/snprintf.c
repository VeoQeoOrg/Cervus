#include <stdio.h>
#include <stdarg.h>

int snprintf(char *buf, size_t sz, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sz, fmt, ap);
    va_end(ap);
    return n;
}

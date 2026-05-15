#include <stdio.h>
#include <stdarg.h>

int fprintf(FILE *s, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    int n = vfprintf(s, fmt, ap);
    va_end(ap);
    return n;
}

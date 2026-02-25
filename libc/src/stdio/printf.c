#include <stdio.h>
#include <stdarg.h>

int printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vprintf(format, args);
    return n;
}
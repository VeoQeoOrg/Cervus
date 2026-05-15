#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

int vfprintf(FILE *s, const char *fmt, va_list ap)
{
    char small[512];
    va_list ap2;
    va_copy(ap2, ap);
    int needed = vsnprintf(small, sizeof(small), fmt, ap2);
    va_end(ap2);
    if (needed < (int)sizeof(small)) {
        fwrite(small, 1, (size_t)needed, s);
        return needed;
    }
    char *big = (char *)malloc((size_t)needed + 1);
    if (!big) {
        fwrite(small, 1, sizeof(small) - 1, s);
        return (int)sizeof(small) - 1;
    }
    vsnprintf(big, (size_t)needed + 1, fmt, ap);
    fwrite(big, 1, (size_t)needed, s);
    free(big);
    return needed;
}

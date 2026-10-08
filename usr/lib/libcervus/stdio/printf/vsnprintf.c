#include <stdio.h>
#include <stdarg.h>
#include "../../musl/vprint.h"

int vsnprintf(char *restrict buf, size_t sz, const char *restrict fmt, va_list ap)
{
    char dummy;
    PSINK s = { sz ? buf : &dummy, sz ? sz : 1, 0, NULL, 0 };
    int r = __cervus_vprint(&s, fmt, ap);
    if (sz) buf[s.pos < sz ? s.pos : sz - 1] = 0;
    return r;
}

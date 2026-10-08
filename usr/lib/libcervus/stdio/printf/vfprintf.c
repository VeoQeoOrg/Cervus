#include <stdio.h>
#include <stdarg.h>
#include "../../musl/vprint.h"

int vfprintf(FILE *restrict stream, const char *restrict fmt, va_list ap)
{
    PSINK s = { NULL, 0, 0, stream, 0 };
    flockfile(stream);
    int r = __cervus_vprint(&s, fmt, ap);
    funlockfile(stream);
    return r;
}

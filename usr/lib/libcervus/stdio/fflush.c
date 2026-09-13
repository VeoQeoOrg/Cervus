#include <stdio.h>
#include <libcervus.h>

int fflush(FILE *s)
{
    if (!s) { __cervus_flush_all(); return 0; }
    return __cervus_fflush(s);
}

#include <stdio.h>
#include <libcervus.h>

int fflush(FILE *s)
{
    if (!s) return 0;
    return __cervus_fflush(s);
}

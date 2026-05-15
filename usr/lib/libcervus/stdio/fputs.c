#include <stdio.h>
#include <string.h>

int fputs(const char *str, FILE *s)
{
    if (!str) return EOF;
    size_t n = strlen(str);
    if (fwrite(str, 1, n, s) != n) return EOF;
    return 0;
}

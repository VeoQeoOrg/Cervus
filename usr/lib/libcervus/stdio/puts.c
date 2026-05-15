#include <stdio.h>

int puts(const char *s)
{
    if (fputs(s, stdout) == EOF) return EOF;
    if (fputc('\n', stdout) == EOF) return EOF;
    return 1;
}

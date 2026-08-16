#include <stdio.h>
#include <libcervus.h>

int ungetc(int c, FILE *f)
{
    if (!f || c == EOF) return EOF;
    if (f->unget) return EOF;
    f->unget = (c & 0xFF) + 1;
    f->eof = 0;
    return c & 0xFF;
}

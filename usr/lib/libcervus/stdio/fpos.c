#include <stdio.h>

int fgetpos(FILE *stream, fpos_t *pos)
{
    if (!pos) return -1;
    long off = ftell(stream);
    if (off < 0) return -1;
    *pos = off;
    return 0;
}

int fsetpos(FILE *stream, const fpos_t *pos)
{
    if (!pos) return -1;
    return fseek(stream, *pos, SEEK_SET);
}

#include <stdio.h>
#include <unistd.h>
#include <libcervus.h>

long ftell(FILE *s)
{
    if (!s) return -1;
    return (long)lseek(s->fd, 0, SEEK_CUR);
}

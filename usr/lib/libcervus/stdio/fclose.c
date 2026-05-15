#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libcervus.h>

int fclose(FILE *s)
{
    if (!s) return EOF;
    int fd = s->fd;
    int owned = s->flags & 1;
    close(fd);
    if (owned) free(s);
    return 0;
}

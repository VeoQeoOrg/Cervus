#include <stdio.h>
#include <errno.h>
#include <libcervus.h>

int fileno(FILE *s)
{
    if (!s || s->has_io) {
        __cervus_errno = EBADF;
        return -1;
    }
    return s->fd;
}

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <errno.h>
#include <libcervus.h>

FILE *fdopen(int fd, const char *mode)
{
    (void)mode;
    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) { __cervus_errno = ENOMEM; return NULL; }
    f->fd       = fd;
    f->eof      = 0;
    f->err      = 0;
    f->flags    = 0;
    f->buf      = NULL;
    f->buf_size = 0;
    f->buf_pos  = 0;
    return f;
}

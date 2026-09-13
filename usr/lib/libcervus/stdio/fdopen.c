#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <libcervus.h>

FILE *fdopen(int fd, const char *mode)
{
    (void)mode;
    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) { __cervus_errno = ENOMEM; return NULL; }
    memset(f, 0, sizeof(*f));
    f->fd    = fd;
    f->flags = 0;
    __cervus_stream_register(f);
    return f;
}

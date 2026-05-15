#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <libcervus.h>

FILE *fopen(const char *path, const char *mode)
{
    if (!path || !mode) return NULL;
    int flags = 0;
    int has_plus = 0;
    for (const char *m = mode + 1; *m; m++) if (*m == '+') has_plus = 1;
    switch (mode[0]) {
        case 'r': flags = has_plus ? O_RDWR : O_RDONLY; break;
        case 'w': flags = (has_plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC; break;
        case 'a': flags = (has_plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND; break;
        default: return NULL;
    }
    int fd = open(path, flags, 0644);
    if (fd < 0) return NULL;
    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) { close(fd); return NULL; }
    f->fd       = fd;
    f->eof      = 0;
    f->err      = 0;
    f->flags    = 1;
    f->buf      = NULL;
    f->buf_size = 0;
    f->buf_pos  = 0;
    return f;
}

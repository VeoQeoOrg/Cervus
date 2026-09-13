#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    memset(f, 0, sizeof(*f));
    f->fd    = fd;
    f->flags = 1;
    __cervus_stream_register(f);
    return f;
}

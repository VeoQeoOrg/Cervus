#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <libcervus.h>

DIR *opendir(const char *path)
{
    int fd = open(path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) return NULL;
    DIR *d = (DIR *)malloc(sizeof(DIR));
    if (!d) { close(fd); return NULL; }
    d->fd = fd;
    return d;
}

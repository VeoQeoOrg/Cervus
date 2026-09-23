#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <libcervus.h>

DIR *fdopendir(int fd)
{
    struct stat st;
    if (fstat(fd, &st) < 0) return NULL;
    if (!S_ISDIR(st.st_mode)) {
        __cervus_errno = ENOTDIR;
        return NULL;
    }
    DIR *d = (DIR *)malloc(sizeof(DIR));
    if (!d) return NULL;
    d->fd = fd;
    return d;
}

#include <unistd.h>
#include <stdlib.h>
#include <libcervus.h>

int closedir(DIR *dirp)
{
    if (!dirp) return -1;
    int fd = dirp->fd;
    free(dirp);
    return close(fd);
}

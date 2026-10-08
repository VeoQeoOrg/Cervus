#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <libcervus.h>

long telldir(DIR *dirp)
{
    if (!dirp) { __cervus_set_errno(EBADF); return -1; }
    return (long)lseek(dirp->fd, 0, SEEK_CUR);
}

void seekdir(DIR *dirp, long loc)
{
    if (dirp) lseek(dirp->fd, (off_t)loc, SEEK_SET);
}

int readdir_r(DIR *restrict dirp, struct dirent *restrict entry, struct dirent **restrict result)
{
    int saved = errno;
    __cervus_set_errno(0);
    struct dirent *d = readdir(dirp);
    int err = errno;
    __cervus_set_errno(saved);
    if (!d) {
        *result = NULL;
        return err;
    }
    memcpy(entry, d, sizeof *entry);
    *result = entry;
    return 0;
}

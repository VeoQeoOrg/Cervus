#include <unistd.h>
#include <stdio.h>
#include <errno.h>

int fchdir(int fd)
{
    char link[64], path[1024];
    if (fd < 0) { errno = EBADF; return -1; }
    snprintf(link, sizeof link, "/proc/self/fd/%d", fd);
    long n = readlink(link, path, sizeof path - 1);
    if (n < 0) { errno = EBADF; return -1; }
    path[n] = '\0';
    return chdir(path);
}

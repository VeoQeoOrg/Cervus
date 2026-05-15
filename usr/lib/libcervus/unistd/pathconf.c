#include <unistd.h>
#include <errno.h>
#include <libcervus.h>

long pathconf(const char *path, int name)
{
    (void)path;
    switch (name) {
        case 0:  return 255;
        case 1:  return 512;
        default: __cervus_errno = EINVAL; return -1;
    }
}

long fpathconf(int fd, int name)
{
    (void)fd;
    switch (name) {
        case 0:  return 255;
        case 1:  return 512;
        default: __cervus_errno = EINVAL; return -1;
    }
}

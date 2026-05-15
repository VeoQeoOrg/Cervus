#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <libcervus.h>

int access(const char *path, int mode)
{
    struct stat st;
    if (!path) { __cervus_errno = EFAULT; return -1; }
    long r = syscall2(SYS_STAT, path, &st);
    (void)mode;
    if (r < 0 && r > -4096) { __cervus_errno = (int)-r; return -1; }
    return 0;
}

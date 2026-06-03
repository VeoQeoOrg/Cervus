#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>
#include <libcervus.h>

int chdir(const char *path)
{
    if (!path || !*path) { __cervus_errno = ENOENT; return -1; }
    int64_t r = __syscall6(SYS_CHDIR, (uint64_t)path, 0, 0, 0, 0, 0);
    if (r < 0) { __cervus_errno = (int)-r; return -1; }
    return 0;
}

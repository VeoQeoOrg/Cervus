#include <sys/stat.h>
#include <sys/syscall.h>
#include <libcervus.h>

int stat(const char *path, struct stat *out)
{
    char abs[CERVUS_PATH_MAX];
    path = __cervus_resolve(path, abs, sizeof(abs));
    return (int)__cervus_sys_ret(syscall2(SYS_STAT, path, out));
}

#include <unistd.h>
#include <sys/syscall.h>
#include <libcervus.h>

int rmdir(const char *path)
{
    char abs[CERVUS_PATH_MAX];
    path = __cervus_resolve(path, abs, sizeof(abs));
    return (int)__cervus_sys_ret(syscall1(SYS_RMDIR, path));
}

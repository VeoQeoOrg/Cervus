#include <sys/stat.h>
#include <sys/syscall.h>
#include <libcervus.h>

int mkdir(const char *path, mode_t mode)
{
    char abs[CERVUS_PATH_MAX];
    path = __cervus_resolve(path, abs, sizeof(abs));
    return (int)__cervus_sys_ret(syscall2(SYS_MKDIR, path, mode));
}

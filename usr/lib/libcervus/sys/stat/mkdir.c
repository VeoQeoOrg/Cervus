#include <sys/stat.h>
#include <sys/syscall.h>
#include <libcervus.h>

int mkdir(const char *path, mode_t mode)
{
    return (int)__cervus_sys_ret(syscall2(SYS_MKDIR, path, mode));
}

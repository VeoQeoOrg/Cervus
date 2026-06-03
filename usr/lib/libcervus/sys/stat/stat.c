#include <sys/stat.h>
#include <sys/syscall.h>
#include <libcervus.h>

int stat(const char *path, struct stat *out)
{
    return (int)__cervus_sys_ret(syscall2(SYS_STAT, path, out));
}

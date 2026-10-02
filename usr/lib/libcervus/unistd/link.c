#include <unistd.h>
#include <sys/syscall.h>
#include <libcervus.h>

int link(const char *oldpath, const char *newpath)
{
    return (int)__cervus_sys_ret(syscall2(SYS_LINK, oldpath, newpath));
}

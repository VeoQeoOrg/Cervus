#include <stdio.h>
#include <sys/syscall.h>
#include <libcervus.h>

int rename(const char *oldp, const char *newp)
{
    return (int)__cervus_sys_ret(syscall2(SYS_RENAME, oldp, newp));
}

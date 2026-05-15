#include <stdio.h>
#include <sys/syscall.h>
#include <libcervus.h>

int rename(const char *oldp, const char *newp)
{
    char absa[CERVUS_PATH_MAX], absb[CERVUS_PATH_MAX];
    oldp = __cervus_resolve(oldp, absa, sizeof(absa));
    newp = __cervus_resolve(newp, absb, sizeof(absb));
    return (int)__cervus_sys_ret(syscall2(SYS_RENAME, oldp, newp));
}

#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

long cervus_statvfs(const char *p, cervus_statvfs_t *o)
{
    return __cervus_sys_ret(syscall2(SYS_STATVFS, p, o));
}

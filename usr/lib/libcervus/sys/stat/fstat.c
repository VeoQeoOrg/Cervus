#include <sys/stat.h>
#include <sys/syscall.h>
#include <libcervus.h>
#include <kstat.h>

int fstat(int fd, struct stat *out)
{
    __cervus_kstat_t k;
    long r = __cervus_sys_ret(syscall2(SYS_FSTAT, fd, &k));
    if (r != 0) return (int)r;
    __cervus_stat_from_kernel(out, &k);
    return 0;
}

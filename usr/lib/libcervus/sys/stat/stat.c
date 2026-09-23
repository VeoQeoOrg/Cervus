#include <sys/stat.h>
#include <sys/syscall.h>
#include <libcervus.h>
#include <kstat.h>

int stat(const char *path, struct stat *out)
{
    __cervus_kstat_t k;
    long r = __cervus_sys_ret(__cervus_kstat(__CERVUS_KSTAT_PATH, (uint64_t)path, &k));
    if (r != 0) return (int)r;
    __cervus_stat_from_kernel(out, &k);
    return 0;
}

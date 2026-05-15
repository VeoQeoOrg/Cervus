#include <libcervus.h>

long __cervus_sys_ret(long r)
{
    if (r < 0 && r > -4096) {
        __cervus_errno = (int)-r;
        return -1;
    }
    return r;
}

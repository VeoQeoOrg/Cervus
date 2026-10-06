#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

int cervus_nanosleep(uint64_t ns)
{
    int cp = __cervus_cp_enter();
    int r = (int)__cervus_sys_ret(syscall1(SYS_SLEEP_NS, ns));
    __cervus_cp_leave(cp);
    return r;
}

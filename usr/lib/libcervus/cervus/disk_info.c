#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

int cervus_disk_info(int i, cervus_disk_info_t *o)
{
    return (int)__cervus_sys_ret(syscall2(SYS_DISK_INFO, (uint64_t)i, o));
}

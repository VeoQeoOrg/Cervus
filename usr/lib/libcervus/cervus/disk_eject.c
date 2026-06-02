#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

int cervus_disk_eject(const char *dev)
{
    return (int)__cervus_sys_ret(syscall1(SYS_DISK_EJECT, dev));
}

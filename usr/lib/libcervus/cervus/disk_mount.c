#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

int cervus_disk_mount(const char *d, const char *p)
{
    return (int)__cervus_sys_ret(syscall2(SYS_DISK_MOUNT, d, p));
}

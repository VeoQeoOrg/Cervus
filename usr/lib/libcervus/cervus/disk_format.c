#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

int cervus_disk_format(const char *d, const char *l, int ext4)
{
    return (int)__cervus_sys_ret(syscall3(SYS_DISK_FORMAT, d, l, (long)ext4));
}

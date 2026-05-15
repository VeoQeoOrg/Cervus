#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

int cervus_disk_partition(const char *d, const cervus_mbr_part_t *s, uint64_t n)
{
    return (int)__cervus_sys_ret(syscall3(SYS_DISK_PARTITION, d, s, n));
}

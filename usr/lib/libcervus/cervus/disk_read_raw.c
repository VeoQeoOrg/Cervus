#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

int cervus_disk_read_raw(const char *d, uint64_t lba, uint64_t c, void *b)
{
    return (int)__cervus_sys_ret(syscall4(SYS_DISK_READ_RAW, d, lba, c, b));
}

#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

int cervus_disk_partition_gpt(const char *dev,
                              const struct cervus_gpt_entry_spec *specs, uint64_t n)
{
    return (int)__cervus_sys_ret(syscall3(SYS_DISK_PARTITION_GPT, dev, specs, n));
}

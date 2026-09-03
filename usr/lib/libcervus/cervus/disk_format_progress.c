#include <sys/cervus.h>
#include <sys/syscall.h>

int cervus_disk_format_progress(void)
{
    return (int)syscall0(SYS_DISK_FORMAT_PROGRESS);
}

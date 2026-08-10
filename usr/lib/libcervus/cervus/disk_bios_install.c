#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

long cervus_disk_bios_install(const char *d, const void *sd, uint32_t ss, int generic)
{
    return __cervus_sys_ret(syscall4(SYS_DISK_BIOS_INSTALL, d, sd, ss, (uint64_t)generic));
}

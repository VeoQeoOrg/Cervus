#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

long cervus_list_mounts(cervus_mount_info_t *o, int m)
{
    return __cervus_sys_ret(syscall2(SYS_LIST_MOUNTS, o, m));
}

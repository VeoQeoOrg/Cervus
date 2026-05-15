#include <sys/cervus.h>
#include <sys/syscall.h>
#include <libcervus.h>

long cervus_disk_list_parts(cervus_part_info_t *o, int m)
{
    return __cervus_sys_ret(syscall2(SYS_DISK_LIST_PARTS, o, m));
}

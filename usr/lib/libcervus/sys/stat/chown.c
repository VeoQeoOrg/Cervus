#include <sys/stat.h>
#include <sys/syscall.h>

extern long __cervus_sys_ret(long r);

int chown(const char *path, uid_t owner, gid_t group)
{
    return (int)__cervus_sys_ret(
        (long)syscall3(SYS_CHOWN, (unsigned long)path,
                       (unsigned long)owner, (unsigned long)group));
}

int fchown(int fd, uid_t owner, gid_t group)
{
    return (int)__cervus_sys_ret(
        (long)syscall3(SYS_FCHOWN, (unsigned long)fd,
                       (unsigned long)owner, (unsigned long)group));
}

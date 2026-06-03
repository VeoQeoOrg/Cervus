#include <unistd.h>
#include <sys/syscall.h>
#include <libcervus.h>

int unlink(const char *path)
{
    return (int)__cervus_sys_ret(syscall1(SYS_UNLINK, path));
}

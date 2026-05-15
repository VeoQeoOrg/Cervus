#include <fcntl.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <libcervus.h>

int open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }
    char abs[CERVUS_PATH_MAX];
    path = __cervus_resolve(path, abs, sizeof(abs));
    return (int)__cervus_sys_ret(syscall3(SYS_OPEN, path, flags, mode));
}

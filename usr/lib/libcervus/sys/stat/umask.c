#include <sys/stat.h>
#include <sys/syscall.h>

mode_t umask(mode_t mask)
{
    return (mode_t)syscall1(SYS_UMASK, (unsigned long)mask);
}

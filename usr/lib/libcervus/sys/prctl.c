#include <sys/prctl.h>
#include <sys/syscall.h>
#include <stdarg.h>
#include <libcervus.h>

int prctl(int option, ...)
{
    va_list ap;
    va_start(ap, option);
    unsigned long a2 = va_arg(ap, unsigned long);
    unsigned long a3 = va_arg(ap, unsigned long);
    unsigned long a4 = va_arg(ap, unsigned long);
    unsigned long a5 = va_arg(ap, unsigned long);
    va_end(ap);
    return (int)__cervus_sys_ret(__syscall6(SYS_PRCTL, (uint64_t)option, a2, a3, a4, a5, 0));
}

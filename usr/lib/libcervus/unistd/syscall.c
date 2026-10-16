#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <libcervus.h>

long syscall(long number, ...)
{
    va_list ap;
    va_start(ap, number);
    long a = va_arg(ap, long), b = va_arg(ap, long), c = va_arg(ap, long);
    long d = va_arg(ap, long), e = va_arg(ap, long), f = va_arg(ap, long);
    va_end(ap);
    return __cervus_sys_ret(__syscall6((uint64_t)number, (uint64_t)a, (uint64_t)b, (uint64_t)c,
                                       (uint64_t)d, (uint64_t)e, (uint64_t)f));
}

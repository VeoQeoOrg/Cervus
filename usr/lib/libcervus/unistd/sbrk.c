#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <sys/syscall.h>
#include <libcervus.h>

void *sbrk(intptr_t incr)
{
    uintptr_t cur = (uintptr_t)syscall1(SYS_BRK, 0);
    if (incr == 0) return (void *)cur;
    uintptr_t nw  = (uintptr_t)syscall1(SYS_BRK, cur + (uintptr_t)incr);
    if (nw != cur + (uintptr_t)incr) {
        __cervus_errno = ENOMEM;
        return (void *)-1;
    }
    return (void *)cur;
}

#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <sys/syscall.h>
#include <libcervus.h>

int brk(void *addr)
{
    uintptr_t r = (uintptr_t)syscall1(SYS_BRK, (uintptr_t)addr);
    if (r != (uintptr_t)addr) { __cervus_errno = ENOMEM; return -1; }
    return 0;
}

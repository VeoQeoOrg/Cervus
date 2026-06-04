#include <sys/mman.h>
#include <sys/syscall.h>
#include <libcervus.h>

int mprotect(void *addr, size_t len, int prot)
{
    (void)addr;
    (void)len;
    (void)prot;
    return 0;
}

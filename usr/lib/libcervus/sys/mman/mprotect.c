#include <sys/mman.h>
#include <sys/syscall.h>
#include <libcervus.h>

int mprotect(void *addr, size_t len, int prot)
{
    return (int)__cervus_sys_ret(syscall3(SYS_MPROTECT, addr, len, prot));
}

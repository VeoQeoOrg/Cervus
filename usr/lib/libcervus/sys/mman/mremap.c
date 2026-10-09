#include <sys/mman.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include <sys/syscall.h>
#include <libcervus.h>

void *mremap(void *old, size_t old_len, size_t new_len, int flags, ...)
{
    void *new_addr = 0;
    if (flags & MREMAP_FIXED) {
        va_list ap;
        va_start(ap, flags);
        new_addr = va_arg(ap, void *);
        va_end(ap);
    }
    long r = syscall5(SYS_MREMAP, (uint64_t)(uintptr_t)old, old_len, new_len, (uint64_t)flags,
                      (uint64_t)(uintptr_t)new_addr);
    if (r < 0 && r > -4096) {
        __cervus_set_errno((int)-r);
        return MAP_FAILED;
    }
    return (void *)r;
}

int mincore(void *addr, size_t len, unsigned char *vec)
{
    return (int)__cervus_sys_ret(syscall3(SYS_MINCORE, (uint64_t)(uintptr_t)addr, len, (uint64_t)(uintptr_t)vec));
}

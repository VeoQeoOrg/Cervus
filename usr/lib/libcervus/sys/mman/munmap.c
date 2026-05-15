#include <sys/mman.h>
#include <sys/syscall.h>
#include <libcervus.h>

int munmap(void *a, size_t l) { return (int)__cervus_sys_ret(syscall2(SYS_MUNMAP, a, l)); }

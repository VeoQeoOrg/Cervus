#include <sys/cervus.h>
#include <sys/syscall.h>

uint64_t cervus_cap_get(void) { return (uint64_t)syscall0(SYS_CAP_GET); }

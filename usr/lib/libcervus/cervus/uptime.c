#include <sys/cervus.h>
#include <sys/syscall.h>

uint64_t cervus_uptime_ns(void) { return (uint64_t)syscall0(SYS_UPTIME); }

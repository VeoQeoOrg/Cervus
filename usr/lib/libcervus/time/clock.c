#include <time.h>
#include <stdint.h>
#include <sys/syscall.h>

clock_t clock(void)
{
    uint64_t up_ns = (uint64_t)syscall0(SYS_UPTIME);
    return (clock_t)(up_ns / 1000ULL);
}

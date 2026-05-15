#include <sys/syscall.h>

void sched_yield_cervus(void) { syscall0(SYS_YIELD); }

int sched_yield(void)
{
    syscall0(SYS_YIELD);
    return 0;
}

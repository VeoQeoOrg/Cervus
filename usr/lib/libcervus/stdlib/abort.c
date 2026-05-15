#include <stdlib.h>
#include <sys/syscall.h>

void abort(void)
{
    syscall1(SYS_EXIT, 134);
    __builtin_unreachable();
}

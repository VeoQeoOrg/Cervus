#include <unistd.h>
#include <sys/syscall.h>

void sync(void)
{
    syscall0(SYS_SYNC);
}

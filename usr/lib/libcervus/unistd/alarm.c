#include <unistd.h>
#include <sys/syscall.h>

unsigned int alarm(unsigned int seconds)
{
    return (unsigned int)syscall1(SYS_ALARM, (unsigned long)seconds);
}

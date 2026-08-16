#include <signal.h>
#include <sys/syscall.h>

extern long __cervus_sys_ret(long r);

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    return (int)__cervus_sys_ret(
        (long)syscall4(SYS_RT_SIGPROCMASK, how, (unsigned long)set, (unsigned long)oldset, 8));
}

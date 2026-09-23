#include <signal.h>
#include <sys/syscall.h>
#include <libcervus.h>

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    return (int)__cervus_sys_ret(
        (long)syscall4(SYS_RT_SIGPROCMASK, how, (unsigned long)set, (unsigned long)oldset, 8));
}

int pthread_sigmask(int how, const sigset_t *set, sigset_t *old)
{
    int saved = __cervus_errno;
    if (sigprocmask(how, set, old) == 0) return 0;
    int e = __cervus_errno;
    __cervus_errno = saved;
    return e;
}

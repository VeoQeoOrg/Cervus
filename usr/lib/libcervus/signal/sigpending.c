#include <signal.h>
#include <string.h>
#include <sys/syscall.h>
#include <libcervus.h>

int sigpending(sigset_t *set)
{
    if (set) memset(set, 0, sizeof *set);
    return (int)__cervus_sys_ret((long)syscall2(SYS_RT_SIGPENDING, (unsigned long)set, 8));
}

int sigsuspend(const sigset_t *mask)
{
    int __cp = __cervus_cp_enter();
    int __r = (int)__cervus_sys_ret((long)syscall2(SYS_RT_SIGSUSPEND, (unsigned long)mask, 8));
    __cervus_cp_leave(__cp);
    return __r;
}

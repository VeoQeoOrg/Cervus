#include <errno.h>
#include <signal.h>
#include <sys/syscall.h>
#include <libcervus.h>

#define SIGCANCEL 32
#define SIGTIMER 33

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    uint64_t mask = 0;
    const uint64_t *p = NULL;
    if (set) {
        mask = set->__bits[0];
        if (how != SIG_UNBLOCK) mask &= ~((1ULL << SIGCANCEL) | (1ULL << SIGTIMER));
        p = &mask;
    }
    uint64_t old = 0;
    long r = syscall4(SYS_RT_SIGPROCMASK, how, (unsigned long)p, oldset ? (unsigned long)&old : 0, 8);
    if (r >= 0 && oldset) {
        oldset->__bits[0] = old;
        oldset->__bits[1] = 0;
    }
    return (int)__cervus_sys_ret(r);
}

int pthread_sigmask(int how, const sigset_t *set, sigset_t *old)
{
    int saved = errno;
    if (sigprocmask(how, set, old) == 0) return 0;
    int e = errno;
    __cervus_set_errno(saved);
    return e;
}

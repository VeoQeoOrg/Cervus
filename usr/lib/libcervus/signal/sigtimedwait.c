#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/syscall.h>
#include <libcervus.h>

#define SIGCANCEL 32
#define SIGTIMER 33

int sigtimedwait(const sigset_t *set, siginfo_t *info, const struct timespec *timeout)
{
    if (!set) { __cervus_set_errno(EINVAL); return -1; }
    uint64_t mask = set->__bits[0] & ~((1ULL << SIGCANCEL) | (1ULL << SIGTIMER));
    int cp = __cervus_cp_enter();
    long r = syscall3(SYS_SIGTIMEDWAIT, (uint64_t)&mask, (uint64_t)info, (uint64_t)timeout);
    __cervus_cp_leave(cp);
    return (int)__cervus_sys_ret(r);
}

int sigwaitinfo(const sigset_t *set, siginfo_t *info)
{
    return sigtimedwait(set, info, NULL);
}

int sigwait(const sigset_t *set, int *sig)
{
    for (;;) {
        int r = sigtimedwait(set, NULL, NULL);
        if (r > 0) {
            if (sig) *sig = r;
            return 0;
        }
        if (errno != EINTR) return errno;
    }
}

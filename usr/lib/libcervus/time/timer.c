#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <libcervus.h>

#define SIGTIMER   33
#define KTIMER_MAX 256

typedef struct {
    int           kid;
    pid_t         tid;
    void        (*fn)(union sigval);
    union sigval  value;
    volatile int  dead;
    volatile int  ready;
} thread_timer_t;

static int fail(long r)
{
    __cervus_set_errno((int)-r);
    return -1;
}

static int kernel_id(timer_t t, thread_timer_t **tt)
{
    uintptr_t v = (uintptr_t)t;
    if (v < KTIMER_MAX) {
        if (tt) *tt = NULL;
        return (int)v;
    }
    thread_timer_t *p = (thread_timer_t *)t;
    if (tt) *tt = p;
    return p->kid;
}

static void *timer_thread(void *arg)
{
    thread_timer_t *t = arg;
    uint64_t mask = 1ULL << SIGTIMER;
    syscall4(SYS_RT_SIGPROCMASK, SIG_BLOCK, (uint64_t)(uintptr_t)&mask, 0, 8);
    __atomic_store_n(&t->tid, gettid(), __ATOMIC_RELEASE);
    __atomic_store_n(&t->ready, 1, __ATOMIC_RELEASE);
    syscall2(SYS_FUTEX_WAKE, (uint64_t)(uintptr_t)&t->ready, 1);
    for (;;) {
        siginfo_t si;
        if (syscall3(SYS_SIGTIMEDWAIT, (uint64_t)(uintptr_t)&mask, (uint64_t)(uintptr_t)&si, 0) < 0) continue;
        if (__atomic_load_n(&t->dead, __ATOMIC_ACQUIRE)) break;
        if (si.si_code == SI_TIMER) t->fn(t->value);
        if (__atomic_load_n(&t->dead, __ATOMIC_ACQUIRE)) break;
    }
    free(t);
    return NULL;
}

static int create_thread_timer(clockid_t clk, const struct sigevent *sev, timer_t *res)
{
    static volatile int ignored;
    if (!__atomic_exchange_n(&ignored, 1, __ATOMIC_ACQ_REL)) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = SIG_IGN;
        sigaction(SIGTIMER, &sa, NULL);
    }
    thread_timer_t *t = calloc(1, sizeof *t);
    if (!t) { __cervus_set_errno(EAGAIN); return -1; }
    t->fn = sev->sigev_notify_function;
    t->value = sev->sigev_value;

    pthread_attr_t attr;
    if (sev->sigev_notify_attributes) attr = *sev->sigev_notify_attributes;
    else pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    sigset_t all, old;
    sigfillset(&all);
    pthread_sigmask(SIG_BLOCK, &all, &old);
    pthread_t th;
    int r = pthread_create(&th, &attr, timer_thread, t);
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    if (r) { free(t); __cervus_set_errno(EAGAIN); return -1; }

    while (!__atomic_load_n(&t->ready, __ATOMIC_ACQUIRE))
        syscall3(SYS_FUTEX_WAIT, (uint64_t)(uintptr_t)&t->ready, 0, 0);

    struct sigevent ksev;
    memset(&ksev, 0, sizeof ksev);
    ksev.sigev_notify = SIGEV_THREAD_ID;
    ksev.sigev_signo = SIGTIMER;
    ksev.sigev_value.sival_ptr = t;
    ksev.sigev_notify_thread_id = t->tid;
    int kid = -1;
    long k = syscall3(SYS_TIMER_CREATE, (uint64_t)(int64_t)clk, (uint64_t)(uintptr_t)&ksev, (uint64_t)(uintptr_t)&kid);
    if (k < 0) {
        __atomic_store_n(&t->dead, 1, __ATOMIC_RELEASE);
        pthread_kill(th, SIGTIMER);
        return fail(k);
    }
    t->kid = kid;
    *res = (timer_t)t;
    return 0;
}

int timer_create(clockid_t clk, struct sigevent *sev, timer_t *res)
{
    if (!res) { __cervus_set_errno(EINVAL); return -1; }
    if (sev && sev->sigev_notify == SIGEV_THREAD) return create_thread_timer(clk, sev, res);
    int kid = -1;
    long r = syscall3(SYS_TIMER_CREATE, (uint64_t)(int64_t)clk, (uint64_t)(uintptr_t)sev, (uint64_t)(uintptr_t)&kid);
    if (r < 0) return fail(r);
    *res = (timer_t)(uintptr_t)kid;
    return 0;
}

int timer_settime(timer_t id, int flags, const struct itimerspec *nv, struct itimerspec *ov)
{
    long r = syscall4(SYS_TIMER_SETTIME, (uint64_t)kernel_id(id, NULL), (uint64_t)flags,
                      (uint64_t)(uintptr_t)nv, (uint64_t)(uintptr_t)ov);
    return r < 0 ? fail(r) : 0;
}

int timer_gettime(timer_t id, struct itimerspec *cur)
{
    long r = syscall2(SYS_TIMER_GETTIME, (uint64_t)kernel_id(id, NULL), (uint64_t)(uintptr_t)cur);
    return r < 0 ? fail(r) : 0;
}

int timer_getoverrun(timer_t id)
{
    long r = syscall1(SYS_TIMER_GETOVERRUN, (uint64_t)kernel_id(id, NULL));
    return r < 0 ? fail(r) : (int)r;
}

int timer_delete(timer_t id)
{
    thread_timer_t *t;
    int kid = kernel_id(id, &t);
    long r = syscall1(SYS_TIMER_DELETE, (uint64_t)kid);
    if (r < 0) return fail(r);
    if (t) {
        pid_t tid = t->tid;
        __atomic_store_n(&t->dead, 1, __ATOMIC_RELEASE);
        if (tid != gettid()) tgkill(getpid(), tid, SIGTIMER);
    }
    return 0;
}

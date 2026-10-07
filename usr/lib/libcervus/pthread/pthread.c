#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <libcervus.h>
#include <sched.h>
#include <time.h>
#include <signal.h>

#define TH_RUNNING  0
#define TH_DONE     1

#define DS_JOINABLE 0
#define DS_DETACHED 1
#define DS_EXITING  2

struct __pthread {
    volatile int  done;
    volatile int  cancel_pending;
    int           cancel_state;
    int           cancel_type;
    volatile int  in_cp;
    struct __pthread_cleanup *cleanup;
    volatile int  dstate;
    volatile int  tid_word;
    struct __pthread *next_zombie;
    void         *retval;
    void         *(*fn)(void *);
    void         *arg;
    void         *stack;
    size_t        stacksize;
    int           tid;
    void         *tls;
    char          name[16];
};

#define DEFAULT_STACK (256 * 1024)

static struct __pthread g_main;

static void futex_wait(volatile int *addr, int val)
{
    syscall3(SYS_FUTEX_WAIT, (uint64_t)(uintptr_t)addr, (uint64_t)val, 0);
}

static void futex_wake(volatile int *addr, int n)
{
    syscall2(SYS_FUTEX_WAKE, (uint64_t)(uintptr_t)addr, (uint64_t)n);
}

#define SIGCANCEL 32

static volatile int g_cancel_used;

static void run_cleanup(struct __pthread *th)
{
    while (th->cleanup) {
        struct __pthread_cleanup *c = th->cleanup;
        th->cleanup = c->next;
        c->fn(c->arg);
    }
}

void __pthread_cleanup_push(struct __pthread_cleanup *c, void (*fn)(void *), void *arg)
{
    struct __pthread *th = pthread_self();
    c->fn = fn;
    c->arg = arg;
    c->next = th->cleanup;
    th->cleanup = c;
}

void __pthread_cleanup_pop(struct __pthread_cleanup *c, int execute)
{
    struct __pthread *th = pthread_self();
    th->cleanup = c->next;
    if (execute) c->fn(c->arg);
}

static void cancel_handler(int sig)
{
    (void)sig;
    struct __pthread *th = pthread_self();
    if (!th->cancel_pending || th->cancel_state != PTHREAD_CANCEL_ENABLE) return;
    if (th->cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS || th->in_cp) {
        th->in_cp = 0;
        th->cancel_state = PTHREAD_CANCEL_DISABLE;
        pthread_exit(PTHREAD_CANCELED);
    }
}

int __cervus_cp_enter(void)
{
    struct __pthread *th = pthread_self();
    if (th->cancel_pending && th->cancel_state == PTHREAD_CANCEL_ENABLE) {
        th->cancel_state = PTHREAD_CANCEL_DISABLE;
        pthread_exit(PTHREAD_CANCELED);
    }
    int old = th->in_cp;
    th->in_cp = 1;
    return old;
}

void __cervus_cp_leave(int old)
{
    pthread_self()->in_cp = old;
}

void pthread_testcancel(void)
{
    if (!__atomic_load_n(&g_cancel_used, __ATOMIC_RELAXED)) return;
    struct __pthread *th = pthread_self();
    if (th->cancel_pending && th->cancel_state == PTHREAD_CANCEL_ENABLE) {
        th->cancel_state = PTHREAD_CANCEL_DISABLE;
        pthread_exit(PTHREAD_CANCELED);
    }
}

int pthread_setcancelstate(int state, int *old)
{
    if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE) return EINVAL;
    struct __pthread *th = pthread_self();
    if (old) *old = th->cancel_state;
    th->cancel_state = state;
    if (state == PTHREAD_CANCEL_ENABLE && th->cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS)
        pthread_testcancel();
    return 0;
}

int pthread_setcanceltype(int type, int *old)
{
    if (type != PTHREAD_CANCEL_DEFERRED && type != PTHREAD_CANCEL_ASYNCHRONOUS) return EINVAL;
    struct __pthread *th = pthread_self();
    if (old) *old = th->cancel_type;
    th->cancel_type = type;
    if (type == PTHREAD_CANCEL_ASYNCHRONOUS) pthread_testcancel();
    return 0;
}

int pthread_kill(pthread_t th, int sig)
{
    if (!th) return ESRCH;
    if (__atomic_load_n(&th->done, __ATOMIC_ACQUIRE) == TH_DONE) return ESRCH;
    if (sig < 0 || sig >= 64) return EINVAL;
    if (!sig) return 0;
    int saved = errno;
    int r = kill(th->tid, sig);
    int e = errno;
    __cervus_set_errno(saved);
    return r < 0 ? e : 0;
}

int pthread_cancel(pthread_t th)
{
    if (!th) return ESRCH;
    if (!__atomic_exchange_n(&g_cancel_used, 1, __ATOMIC_ACQ_REL)) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = cancel_handler;
        sigaction(SIGCANCEL, &sa, NULL);
    }
    __atomic_store_n(&th->cancel_pending, 1, __ATOMIC_RELEASE);
    if (th == pthread_self()) {
        if (th->cancel_state == PTHREAD_CANCEL_ENABLE && th->cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS) {
            th->cancel_state = PTHREAD_CANCEL_DISABLE;
            pthread_exit(PTHREAD_CANCELED);
        }
        return 0;
    }
    if (th->cancel_state == PTHREAD_CANCEL_ENABLE &&
        __atomic_load_n(&th->done, __ATOMIC_ACQUIRE) != TH_DONE && th->tid > 0)
        kill(th->tid, SIGCANCEL);
    return 0;
}

static struct __pthread *g_zombies;

__attribute__((noreturn)) static void unmap_and_exit(void *stk, size_t sz)
{
    __asm__ volatile(
        "syscall\n\t"
        "mov %3, %%eax\n\t"
        "xor %%edi, %%edi\n\t"
        "syscall\n\t"
        "1: jmp 1b"
        :: "a"((long)SYS_MUNMAP), "D"(stk), "S"(sz), "i"(SYS_THREAD_EXIT)
        : "rcx", "r11", "memory");
    __builtin_unreachable();
}

static void wait_gone(struct __pthread *th)
{
    for (;;) {
        int v = __atomic_load_n(&th->tid_word, __ATOMIC_ACQUIRE);
        if (!v) return;
        futex_wait(&th->tid_word, v);
    }
}

static void release(struct __pthread *th)
{
    void *stk = th->stack;
    size_t sz = th->stacksize;
    void *tls = th->tls;
    free(th);
    __cervus_tls_free(tls);
    if (stk) munmap(stk, sz);
}

static void reap_zombies(void)
{
    struct __pthread *list = __atomic_exchange_n(&g_zombies, NULL, __ATOMIC_ACQ_REL);
    while (list) {
        struct __pthread *th = list;
        list = th->next_zombie;
        if (__atomic_load_n(&th->tid_word, __ATOMIC_ACQUIRE)) {
            th->next_zombie = __atomic_load_n(&g_zombies, __ATOMIC_RELAXED);
            while (!__atomic_compare_exchange_n(&g_zombies, &th->next_zombie, th, 0,
                                                __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {}
            continue;
        }
        void *tls = th->tls;
        free(th);
        __cervus_tls_free(tls);
    }
}

__attribute__((noreturn)) static void thread_finish(struct __pthread *th, void *retval)
{
    run_cleanup(th);
    __cervus_pthread_key_cleanup();
    th->retval = retval;
    __atomic_store_n(&th->done, TH_DONE, __ATOMIC_RELEASE);
    futex_wake(&th->done, 1);

    int st = DS_JOINABLE;
    if (!__atomic_compare_exchange_n(&th->dstate, &st, DS_EXITING, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        void *stk = th->stack;
        size_t sz = th->stacksize;
        th->stack = NULL;
        th->next_zombie = __atomic_load_n(&g_zombies, __ATOMIC_RELAXED);
        while (!__atomic_compare_exchange_n(&g_zombies, &th->next_zombie, th, 0,
                                            __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {}
        unmap_and_exit(stk, sz);
    }

    syscall1(SYS_THREAD_EXIT, 0);
    for (;;) {}
}

void __cervus_thread_entry(struct __pthread *th)
{
    __cervus_tls_set(th->tls);
    th->tid = (int)getpid();
    thread_finish(th, th->fn(th->arg));
}

extern void __cervus_thread_trampoline(void);

int pthread_create(pthread_t *out, const pthread_attr_t *attr,
                   void *(*fn)(void *), void *arg)
{
    if (!out || !fn) return EINVAL;

    size_t ssz = (attr && attr->stacksize) ? attr->stacksize : DEFAULT_STACK;
    ssz = (ssz + 0xFFF) & ~(size_t)0xFFF;

    reap_zombies();

    struct __pthread *th = calloc(1, sizeof(*th));
    if (!th) return EAGAIN;

    th->tls = __cervus_tls_alloc();
    if (!th->tls) { free(th); return EAGAIN; }
    ((void **)th->tls)[__CERVUS_TCB_SELF] = th;

    void *stk = mmap(NULL, ssz, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stk == MAP_FAILED) { __cervus_tls_free(th->tls); free(th); return EAGAIN; }

    th->fn = fn;
    th->arg = arg;
    th->stack = stk;
    th->stacksize = ssz;
    th->done = TH_RUNNING;
    th->dstate = (attr && attr->detachstate == PTHREAD_CREATE_DETACHED) ? DS_DETACHED : DS_JOINABLE;
    th->tid_word = -1;

    uintptr_t top = ((uintptr_t)stk + ssz) & ~(uintptr_t)0xF;
    uint64_t *sp = (uint64_t *)top;
    *--sp = (uint64_t)(uintptr_t)th;

    long tid = syscall4(SYS_THREAD_CREATE,
                        (uint64_t)(uintptr_t)__cervus_thread_trampoline,
                        (uint64_t)(uintptr_t)sp, 0, (uint64_t)(uintptr_t)&th->tid_word);
    if (tid < 0) {
        munmap(stk, ssz);
        __cervus_tls_free(th->tls);
        free(th);
        return EAGAIN;
    }

    *out = th;
    return 0;
}

int pthread_join(pthread_t th, void **retval)
{
    if (!th) return EINVAL;
    if (th == pthread_self()) return EDEADLK;
    if (th == &g_main) return EINVAL;
    int cp = __cervus_cp_enter();
    while (__atomic_load_n(&th->done, __ATOMIC_ACQUIRE) != TH_DONE)
        futex_wait(&th->done, TH_RUNNING);
    __cervus_cp_leave(cp);
    wait_gone(th);

    if (retval) *retval = th->retval;
    release(th);
    reap_zombies();
    return 0;
}

int pthread_detach(pthread_t th)
{
    if (!th || th == &g_main) return EINVAL;
    int st = DS_JOINABLE;
    if (__atomic_compare_exchange_n(&th->dstate, &st, DS_DETACHED, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return 0;
    if (st == DS_DETACHED) return EINVAL;
    wait_gone(th);
    release(th);
    return 0;
}

void pthread_exit(void *retval)
{
    struct __pthread *th = pthread_self();
    if (th && th != &g_main) thread_finish(th, retval);
    if (th) run_cleanup(th);
    __cervus_pthread_key_cleanup();
    if (th) {
        th->retval = retval;
        __atomic_store_n(&th->done, TH_DONE, __ATOMIC_RELEASE);
        futex_wake(&th->done, 1);
    }
    syscall1(SYS_THREAD_EXIT, 0);
    for (;;) {}
}

pthread_t pthread_self(void)
{
    void **tcb = __cervus_tcb();
    struct __pthread *self = tcb[__CERVUS_TCB_SELF];
    if (!self) {
        g_main.tid = (int)getpid();
        self = &g_main;
        tcb[__CERVUS_TCB_SELF] = self;
    }
    return self;
}
int pthread_equal(pthread_t a, pthread_t b) { return a == b; }

int pthread_attr_init(pthread_attr_t *a)
{
    if (!a) return EINVAL;
    a->stacksize = DEFAULT_STACK;
    a->detachstate = PTHREAD_CREATE_JOINABLE;
    return 0;
}
int pthread_attr_destroy(pthread_attr_t *a) { (void)a; return 0; }
int pthread_attr_setstacksize(pthread_attr_t *a, size_t sz)
{
    if (!a || sz < 16384) return EINVAL;
    a->stacksize = sz;
    return 0;
}
int pthread_attr_getstacksize(const pthread_attr_t *a, size_t *sz)
{
    if (!a || !sz) return EINVAL;
    *sz = a->stacksize;
    return 0;
}
int pthread_attr_setdetachstate(pthread_attr_t *a, int state)
{
    if (!a) return EINVAL;
    a->detachstate = state;
    return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t *a)
{
    if (!a) return EINVAL;
    a->type = PTHREAD_MUTEX_DEFAULT;
    a->pshared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *a) { return a ? 0 : EINVAL; }

int pthread_mutexattr_settype(pthread_mutexattr_t *a, int type)
{
    if (!a || type < PTHREAD_MUTEX_NORMAL || type > PTHREAD_MUTEX_ERRORCHECK) return EINVAL;
    a->type = type;
    return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *a, int *type)
{
    if (!a || !type) return EINVAL;
    *type = a->type;
    return 0;
}

int pthread_mutexattr_setpshared(pthread_mutexattr_t *a, int pshared)
{
    if (!a || (pshared != PTHREAD_PROCESS_PRIVATE && pshared != PTHREAD_PROCESS_SHARED)) return EINVAL;
    a->pshared = pshared;
    return 0;
}

int pthread_mutexattr_getpshared(const pthread_mutexattr_t *a, int *pshared)
{
    if (!a || !pshared) return EINVAL;
    *pshared = a->pshared;
    return 0;
}

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a)
{
    if (!m) return EINVAL;
    m->state = 0;
    m->type = a ? a->type : PTHREAD_MUTEX_DEFAULT;
    m->owner = 0;
    m->count = 0;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *m) { (void)m; return 0; }

static void mutex_acquire(pthread_mutex_t *m)
{
    int zero = 0;
    if (__atomic_compare_exchange_n(&m->state, &zero, 1, 0,
                                    __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE))
        return;
    for (;;) {
        int prev = __atomic_exchange_n(&m->state, 2, __ATOMIC_ACQUIRE);
        if (prev == 0) return;
        futex_wait(&m->state, 2);
    }
}

int pthread_mutex_lock(pthread_mutex_t *m)
{
    if (!m) return EINVAL;
    if (m->type != PTHREAD_MUTEX_NORMAL) {
        void *self = pthread_self();
        if (m->owner == self) {
            if (m->type == PTHREAD_MUTEX_ERRORCHECK) return EDEADLK;
            m->count++;
            return 0;
        }
        mutex_acquire(m);
        m->owner = self;
        m->count = 1;
        return 0;
    }
    mutex_acquire(m);
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m)
{
    if (!m) return EINVAL;
    void *self = m->type != PTHREAD_MUTEX_NORMAL ? (void *)pthread_self() : 0;
    if (self && m->owner == self) {
        if (m->type == PTHREAD_MUTEX_ERRORCHECK) return EBUSY;
        m->count++;
        return 0;
    }
    int zero = 0;
    if (!__atomic_compare_exchange_n(&m->state, &zero, 1, 0,
                                     __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE))
        return EBUSY;
    if (self) {
        m->owner = self;
        m->count = 1;
    }
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *m)
{
    if (!m) return EINVAL;
    if (m->type != PTHREAD_MUTEX_NORMAL) {
        if (m->owner != (void *)pthread_self()) return EPERM;
        if (--m->count > 0) return 0;
        m->owner = 0;
    }
    int prev = __atomic_exchange_n(&m->state, 0, __ATOMIC_RELEASE);
    if (prev == 2) futex_wake(&m->state, 1);
    return 0;
}

static void cond_clock_set(pthread_cond_t *c, int clock);

int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a)
{
    if (!c) return EINVAL;
    c->seq = 0;
    cond_clock_set(c, a ? a->unused : CLOCK_REALTIME);
    return 0;
}
int pthread_cond_destroy(pthread_cond_t *c) { if (c) cond_clock_set(c, CLOCK_REALTIME); return 0; }

static void cond_relock(void *m)
{
    pthread_mutex_lock(m);
}

int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
    if (!c || !m) return EINVAL;
    int seq = __atomic_load_n(&c->seq, __ATOMIC_ACQUIRE);
    struct __pthread_cleanup cb;
    __pthread_cleanup_push(&cb, cond_relock, m);
    pthread_mutex_unlock(m);
    int cp = __cervus_cp_enter();
    futex_wait(&c->seq, seq);
    __cervus_cp_leave(cp);
    __pthread_cleanup_pop(&cb, 1);
    return 0;
}

int pthread_cond_signal(pthread_cond_t *c)
{
    if (!c) return EINVAL;
    __atomic_add_fetch(&c->seq, 1, __ATOMIC_RELEASE);
    futex_wake(&c->seq, 1);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *c)
{
    if (!c) return EINVAL;
    __atomic_add_fetch(&c->seq, 1, __ATOMIC_RELEASE);
    futex_wake(&c->seq, 0x7FFFFFFF);
    return 0;
}

int pthread_once(pthread_once_t *o, void (*fn)(void))
{
    if (!o || !fn) return EINVAL;
    int zero = 0;
    if (__atomic_compare_exchange_n(&o->state, &zero, 1, 0,
                                    __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) {
        fn();
        __atomic_store_n(&o->state, 2, __ATOMIC_RELEASE);
        futex_wake(&o->state, 0x7FFFFFFF);
        return 0;
    }
    while (__atomic_load_n(&o->state, __ATOMIC_ACQUIRE) != 2)
        futex_wait(&o->state, 1);
    return 0;
}

int pthread_rwlock_init(pthread_rwlock_t *l, void *a)
{
    (void)a;
    if (!l) return EINVAL;
    l->readers = 0;
    return pthread_mutex_init(&l->w, NULL);
}
int pthread_rwlock_destroy(pthread_rwlock_t *l) { (void)l; return 0; }

int pthread_rwlock_rdlock(pthread_rwlock_t *l)
{
    if (!l) return EINVAL;
    pthread_mutex_lock(&l->w);
    __atomic_add_fetch(&l->readers, 1, __ATOMIC_ACQUIRE);
    pthread_mutex_unlock(&l->w);
    return 0;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *l)
{
    if (!l) return EINVAL;
    pthread_mutex_lock(&l->w);
    while (__atomic_load_n(&l->readers, __ATOMIC_ACQUIRE) > 0)
        futex_wait(&l->readers, __atomic_load_n(&l->readers, __ATOMIC_ACQUIRE));
    return 0;
}

int pthread_rwlock_unlock(pthread_rwlock_t *l)
{
    if (!l) return EINVAL;
    if (__atomic_load_n(&l->readers, __ATOMIC_ACQUIRE) > 0) {
        if (__atomic_sub_fetch(&l->readers, 1, __ATOMIC_RELEASE) == 0)
            futex_wake(&l->readers, 1);
        return 0;
    }
    return pthread_mutex_unlock(&l->w);
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *l)
{
    if (!l) return EINVAL;
    if (pthread_mutex_trylock(&l->w) != 0) return EBUSY;
    __atomic_add_fetch(&l->readers, 1, __ATOMIC_ACQUIRE);
    pthread_mutex_unlock(&l->w);
    return 0;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *l)
{
    if (!l) return EINVAL;
    if (pthread_mutex_trylock(&l->w) != 0) return EBUSY;
    if (__atomic_load_n(&l->readers, __ATOMIC_ACQUIRE) > 0) {
        pthread_mutex_unlock(&l->w);
        return EBUSY;
    }
    return 0;
}

int pthread_condattr_init(pthread_condattr_t *a) { if (!a) return EINVAL; a->unused = CLOCK_REALTIME; return 0; }
int pthread_condattr_destroy(pthread_condattr_t *a) { (void)a; return 0; }
int pthread_condattr_setpshared(pthread_condattr_t *a, int pshared) { (void)a; (void)pshared; return 0; }
int pthread_condattr_getpshared(const pthread_condattr_t *a, int *pshared) { (void)a; *pshared = PTHREAD_PROCESS_PRIVATE; return 0; }

int pthread_condattr_setclock(pthread_condattr_t *a, int clock)
{
    if (!a || (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC)) return EINVAL;
    a->unused = clock;
    return 0;
}

int pthread_condattr_getclock(const pthread_condattr_t *a, int *clock)
{
    if (!a || !clock) return EINVAL;
    *clock = a->unused;
    return 0;
}

void futex_lock(volatile uint32_t *lock);
void futex_unlock(volatile uint32_t *lock);

#define COND_CLOCKS 64
static struct { pthread_cond_t *c; int clock; } g_cond_clock[COND_CLOCKS];
static volatile uint32_t g_cond_clock_lock;

static void cond_clock_set(pthread_cond_t *c, int clock)
{
    futex_lock(&g_cond_clock_lock);
    int free_slot = -1;
    for (int i = 0; i < COND_CLOCKS; i++) {
        if (g_cond_clock[i].c == c) { free_slot = i; break; }
        if (!g_cond_clock[i].c && free_slot < 0) free_slot = i;
    }
    if (free_slot >= 0) {
        g_cond_clock[free_slot].c = clock == CLOCK_REALTIME ? NULL : c;
        g_cond_clock[free_slot].clock = clock;
    }
    futex_unlock(&g_cond_clock_lock);
}

static int cond_clock_get(pthread_cond_t *c)
{
    int clock = CLOCK_REALTIME;
    futex_lock(&g_cond_clock_lock);
    for (int i = 0; i < COND_CLOCKS; i++)
        if (g_cond_clock[i].c == c) { clock = g_cond_clock[i].clock; break; }
    futex_unlock(&g_cond_clock_lock);
    return clock;
}

int pthread_cond_clockwait(pthread_cond_t *c, pthread_mutex_t *m, int clock, const struct timespec *abstime)
{
    if (!c || !m || !abstime) return EINVAL;
    if (abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000L) return EINVAL;
    struct timespec now;
    clock_gettime(clock, &now);
    int64_t left = (int64_t)(abstime->tv_sec - now.tv_sec) * 1000000000LL + (abstime->tv_nsec - now.tv_nsec);
    if (left <= 0) return ETIMEDOUT;
    int seq = __atomic_load_n(&c->seq, __ATOMIC_ACQUIRE);
    struct __pthread_cleanup cb;
    __pthread_cleanup_push(&cb, cond_relock, m);
    pthread_mutex_unlock(m);
    int cp = __cervus_cp_enter();
    long r = (long)syscall3(SYS_FUTEX_WAIT, (uint64_t)(uintptr_t)&c->seq, (uint64_t)(unsigned)seq, (uint64_t)left);
    __cervus_cp_leave(cp);
    __pthread_cleanup_pop(&cb, 1);
    if (r == -ETIMEDOUT && __atomic_load_n(&c->seq, __ATOMIC_ACQUIRE) == seq) return ETIMEDOUT;
    return 0;
}

int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *abstime)
{
    return pthread_cond_clockwait(c, m, cond_clock_get(c), abstime);
}

int pthread_getschedparam(pthread_t th, int *policy, struct sched_param *param)
{
    (void)th;
    if (policy) *policy = SCHED_OTHER;
    if (param) param->sched_priority = 0;
    return 0;
}

int pthread_setschedparam(pthread_t th, int policy, const struct sched_param *param)
{
    (void)th;
    if (!param) return EINVAL;
    if (policy != SCHED_OTHER && policy != SCHED_BATCH && policy != SCHED_IDLE) return EPERM;
    return param->sched_priority == 0 ? 0 : EINVAL;
}

int pthread_setschedprio(pthread_t th, int prio)
{
    (void)th;
    return prio == 0 ? 0 : EINVAL;
}

int pthread_attr_getdetachstate(const pthread_attr_t *a, int *state)
{
    if (!a || !state) return EINVAL;
    *state = a->detachstate;
    return 0;
}

int pthread_setname_np(pthread_t th, const char *name)
{
    if (!th || !name) return EINVAL;
    if (strlen(name) >= sizeof th->name) return ERANGE;
    strcpy(th->name, name);
    return 0;
}

int pthread_getname_np(pthread_t th, char *name, size_t len)
{
    if (!th || !name) return EINVAL;
    if (strlen(th->name) >= len) return ERANGE;
    strcpy(name, th->name);
    return 0;
}

#define ATFORK_MAX 32
static struct { void (*prepare)(void); void (*parent)(void); void (*child)(void); } g_atfork[ATFORK_MAX];
static int g_atfork_n;

int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void))
{
    if (g_atfork_n >= ATFORK_MAX) return ENOMEM;
    g_atfork[g_atfork_n].prepare = prepare;
    g_atfork[g_atfork_n].parent = parent;
    g_atfork[g_atfork_n].child = child;
    g_atfork_n++;
    return 0;
}

void __cervus_atfork_prepare(void)
{
    for (int i = g_atfork_n - 1; i >= 0; i--) if (g_atfork[i].prepare) g_atfork[i].prepare();
}

void __cervus_atfork_parent(void)
{
    for (int i = 0; i < g_atfork_n; i++) if (g_atfork[i].parent) g_atfork[i].parent();
}

void __cervus_atfork_child(void)
{
    for (int i = 0; i < g_atfork_n; i++) if (g_atfork[i].child) g_atfork[i].child();
}

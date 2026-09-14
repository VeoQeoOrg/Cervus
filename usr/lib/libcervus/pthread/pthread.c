#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <libcervus.h>

#define TH_RUNNING  0
#define TH_DONE     1

struct __pthread {
    volatile int  done;
    volatile int  detached;
    void         *retval;
    void         *(*fn)(void *);
    void         *arg;
    void         *stack;
    size_t        stacksize;
    int           tid;
    void         *tls;
};

#define DEFAULT_STACK (256 * 1024)

static __thread struct __pthread *g_self;
static struct __pthread g_main;

static void futex_wait(volatile int *addr, int val)
{
    syscall3(SYS_FUTEX_WAIT, (uint64_t)(uintptr_t)addr, (uint64_t)val, 0);
}

static void futex_wake(volatile int *addr, int n)
{
    syscall2(SYS_FUTEX_WAKE, (uint64_t)(uintptr_t)addr, (uint64_t)n);
}

void __cervus_thread_entry(struct __pthread *th)
{
    void *tp = __cervus_tls_alloc();
    if (tp) __cervus_tls_set(tp);
    th->tls = tp;
    g_self = th;
    th->tid = (int)getpid();
    void *r = th->fn(th->arg);

    th->retval = r;
    __atomic_store_n(&th->done, TH_DONE, __ATOMIC_RELEASE);
    futex_wake(&th->done, 1);

    if (__atomic_load_n(&th->detached, __ATOMIC_ACQUIRE)) {
        void *stk = th->stack;
        size_t sz = th->stacksize;
        void *mytls = th->tls;
        free(th);
        __cervus_tls_free(mytls);
        if (stk) munmap(stk, sz);
    }

    syscall1(SYS_THREAD_EXIT, 0);
    for (;;) {}
}

extern void __cervus_thread_trampoline(void);

int pthread_create(pthread_t *out, const pthread_attr_t *attr,
                   void *(*fn)(void *), void *arg)
{
    if (!out || !fn) return EINVAL;

    size_t ssz = (attr && attr->stacksize) ? attr->stacksize : DEFAULT_STACK;
    ssz = (ssz + 0xFFF) & ~(size_t)0xFFF;

    struct __pthread *th = calloc(1, sizeof(*th));
    if (!th) return EAGAIN;

    void *stk = mmap(NULL, ssz, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stk == MAP_FAILED) { free(th); return EAGAIN; }

    th->fn = fn;
    th->arg = arg;
    th->stack = stk;
    th->stacksize = ssz;
    th->done = TH_RUNNING;
    th->detached = (attr && attr->detachstate == PTHREAD_CREATE_DETACHED);

    uintptr_t top = ((uintptr_t)stk + ssz) & ~(uintptr_t)0xF;
    uint64_t *sp = (uint64_t *)top;
    *--sp = (uint64_t)(uintptr_t)th;

    long tid = syscall3(SYS_THREAD_CREATE,
                        (uint64_t)(uintptr_t)__cervus_thread_trampoline,
                        (uint64_t)(uintptr_t)sp, 0);
    if (tid < 0) {
        munmap(stk, ssz);
        free(th);
        return EAGAIN;
    }

    *out = th;
    return 0;
}

int pthread_join(pthread_t th, void **retval)
{
    if (!th) return EINVAL;
    while (__atomic_load_n(&th->done, __ATOMIC_ACQUIRE) != TH_DONE)
        futex_wait(&th->done, TH_RUNNING);

    if (retval) *retval = th->retval;

    void *stk = th->stack;
    size_t sz = th->stacksize;
    void *tls = th->tls;
    free(th);
    __cervus_tls_free(tls);
    if (stk) munmap(stk, sz);
    return 0;
}

int pthread_detach(pthread_t th)
{
    if (!th) return EINVAL;
    __atomic_store_n(&th->detached, 1, __ATOMIC_RELEASE);
    return 0;
}

void pthread_exit(void *retval)
{
    struct __pthread *th = pthread_self();
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
    if (!g_self) { g_main.tid = (int)getpid(); g_self = &g_main; }
    return g_self;
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

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a)
{
    (void)a;
    if (!m) return EINVAL;
    m->state = 0;
    m->type = 0;
    return 0;
}
int pthread_mutex_destroy(pthread_mutex_t *m) { (void)m; return 0; }

int pthread_mutex_lock(pthread_mutex_t *m)
{
    if (!m) return EINVAL;
    int zero = 0;
    if (__atomic_compare_exchange_n(&m->state, &zero, 1, 0,
                                    __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE))
        return 0;
    for (;;) {
        int prev = __atomic_exchange_n(&m->state, 2, __ATOMIC_ACQUIRE);
        if (prev == 0) return 0;
        futex_wait(&m->state, 2);
    }
}

int pthread_mutex_trylock(pthread_mutex_t *m)
{
    if (!m) return EINVAL;
    int zero = 0;
    if (__atomic_compare_exchange_n(&m->state, &zero, 1, 0,
                                    __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE))
        return 0;
    return EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t *m)
{
    if (!m) return EINVAL;
    int prev = __atomic_exchange_n(&m->state, 0, __ATOMIC_RELEASE);
    if (prev == 2) futex_wake(&m->state, 1);
    return 0;
}

int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a)
{
    (void)a;
    if (!c) return EINVAL;
    c->seq = 0;
    return 0;
}
int pthread_cond_destroy(pthread_cond_t *c) { (void)c; return 0; }

int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
    if (!c || !m) return EINVAL;
    int seq = __atomic_load_n(&c->seq, __ATOMIC_ACQUIRE);
    pthread_mutex_unlock(m);
    futex_wait(&c->seq, seq);
    pthread_mutex_lock(m);
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

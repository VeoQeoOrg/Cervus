#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include "darwin.h"

#define SIG_MUTEX_INIT      0x32aaaba7L
#define SIG_MUTEX_ERRCHK    0x32aaaba1L
#define SIG_MUTEX_RECURSIVE 0x32aaaba2L
#define SIG_MUTEX_FIRSTFIT  0x32aaaba3L
#define SIG_MUTEX           0x4d555458L
#define SIG_COND            0x434f4e44L
#define SIG_RWLOCK          0x52574c4bL
#define SIG_ATTR            0x54484441L
#define SIG_MUTEXATTR       0x4d545841L
#define SIG_CONDATTR        0x434e4441L
#define SIG_RWLOCKATTR      0x52574c41L

struct dobj {
    long sig;
    unsigned char inner[];
};

#define INNER(p, T) ((T *)(void *)((struct dobj *)(p))->inner)

_Static_assert(sizeof(pthread_mutex_t) <= 56, "mutex fits Darwin pthread_mutex_t");
_Static_assert(sizeof(pthread_cond_t) <= 40, "cond fits Darwin pthread_cond_t");
_Static_assert(sizeof(pthread_rwlock_t) <= 192, "rwlock fits Darwin pthread_rwlock_t");
_Static_assert(sizeof(pthread_attr_t) <= 56, "attr fits Darwin pthread_attr_t");
_Static_assert(sizeof(pthread_mutexattr_t) <= 8, "mutexattr fits Darwin pthread_mutexattr_t");
_Static_assert(sizeof(pthread_once_t) <= 8, "once fits Darwin pthread_once_t");

struct dpthread {
    long sig;
    void *cleanup_stack;
    pthread_t real;
    void *(*fn)(void *);
    void *arg;
    uint64_t tid;
    int main;
};

static struct dpthread g_main_thread = { .sig = SIG_ATTR, .main = 1 };
static __thread struct dpthread *t_self;

static int rc(int e)
{
    return e ? d_errno_lx2d(e) : 0;
}

static struct dpthread *self(void)
{
    if (!t_self) {
        t_self = &g_main_thread;
        g_main_thread.real = pthread_self();
        g_main_thread.tid = (uint64_t)gettid();
    }
    return t_self;
}

static void *tramp(void *p)
{
    struct dpthread *d = p;
    t_self = d;
    d->tid = (uint64_t)gettid();
    return d->fn(d->arg);
}

static int d_pthread_create(struct dpthread **out, const struct dobj *attr, void *(*fn)(void *), void *arg)
{
    struct dpthread *d = calloc(1, sizeof *d);
    if (!d)
        return 35;
    d->sig = SIG_ATTR;
    d->fn = fn;
    d->arg = arg;
    self();
    int e = pthread_create(&d->real, attr ? INNER(attr, pthread_attr_t) : NULL, tramp, d);
    if (e) {
        free(d);
        return rc(e);
    }
    *out = d;
    return 0;
}

static int d_pthread_join(struct dpthread *d, void **ret)
{
    int e = pthread_join(d->real, ret);
    if (!e && !d->main)
        free(d);
    return rc(e);
}

static int d_pthread_detach(struct dpthread *d) { return rc(pthread_detach(d->real)); }
static struct dpthread *d_pthread_self(void) { return self(); }
static int d_pthread_equal(struct dpthread *a, struct dpthread *b) { return a == b; }
static void d_pthread_exit(void *v) { pthread_exit(v); }
static int d_pthread_cancel(struct dpthread *d) { return rc(pthread_cancel(d->real)); }

static int d_pthread_kill(struct dpthread *d, int sig)
{
    int l = sig ? d_sig_d2lx(sig) : 0;
    if (sig && !l)
        return 22;
    return rc(pthread_kill(d->real, l));
}
static int d_pthread_main_np(void) { return self() == &g_main_thread; }

static int d_pthread_setcancelstate(int st, int *old)
{
    int o, e = pthread_setcancelstate(st == 1 ? PTHREAD_CANCEL_ENABLE : PTHREAD_CANCEL_DISABLE, &o);
    if (old)
        *old = o == PTHREAD_CANCEL_ENABLE ? 1 : 0;
    return rc(e);
}

static int d_pthread_setcanceltype(int ty, int *old)
{
    int o, e = pthread_setcanceltype(ty == 2 ? PTHREAD_CANCEL_DEFERRED : PTHREAD_CANCEL_ASYNCHRONOUS, &o);
    if (old)
        *old = o == PTHREAD_CANCEL_DEFERRED ? 2 : 0;
    return rc(e);
}

static int d_pthread_threadid_np(struct dpthread *d, uint64_t *id)
{
    if (!d)
        d = self();
    if (id)
        *id = d->tid ? d->tid : (uint64_t)(uintptr_t)d->real;
    return 0;
}

static int d_pthread_setname_np(const char *name) { return rc(pthread_setname_np(pthread_self(), name)); }
static int d_pthread_getname_np(struct dpthread *d, char *b, size_t n) { return rc(pthread_getname_np(d->real, b, n)); }

static void *d_pthread_get_stackaddr_np(struct dpthread *d)
{
    pthread_attr_t a;
    void *addr = NULL;
    size_t sz = 0;
    if (pthread_getattr_np(d->real, &a) == 0) {
        pthread_attr_getstack(&a, &addr, &sz);
        pthread_attr_destroy(&a);
    }
    return (char *)addr + sz;
}

static size_t d_pthread_get_stacksize_np(struct dpthread *d)
{
    pthread_attr_t a;
    void *addr = NULL;
    size_t sz = 0;
    if (pthread_getattr_np(d->real, &a) == 0) {
        pthread_attr_getstack(&a, &addr, &sz);
        pthread_attr_destroy(&a);
    }
    return sz ? sz : 8u << 20;
}

static unsigned d_pthread_mach_thread_np(struct dpthread *d) { return (unsigned)(d->tid ? d->tid : 1); }
static void d_pthread_yield_np(void) { sched_yield(); }
static void d_pthread_jit_write_protect_np(int on) { (void)on; }
static int d_qos_zero(void) { return 0; }

DSYM("_pthread_create", d_pthread_create);
DSYM("_pthread_join", d_pthread_join);
DSYM("_pthread_detach", d_pthread_detach);
DSYM("_pthread_self", d_pthread_self);
DSYM("_pthread_equal", d_pthread_equal);
DSYM("_pthread_exit", d_pthread_exit);
DSYM("_pthread_cancel", d_pthread_cancel);
DSYM("_pthread_kill", d_pthread_kill);
DSYM("_pthread_main_np", d_pthread_main_np);
DSYM("_pthread_setcancelstate", d_pthread_setcancelstate);
DSYM("_pthread_setcanceltype", d_pthread_setcanceltype);
DSYM("_pthread_threadid_np", d_pthread_threadid_np);
DSYM("_pthread_setname_np", d_pthread_setname_np);
DSYM("_pthread_getname_np", d_pthread_getname_np);
DSYM("_pthread_get_stackaddr_np", d_pthread_get_stackaddr_np);
DSYM("_pthread_get_stacksize_np", d_pthread_get_stacksize_np);
DSYM("_pthread_mach_thread_np", d_pthread_mach_thread_np);
DSYM("_pthread_yield_np", d_pthread_yield_np);
DSYM("_pthread_jit_write_protect_np", d_pthread_jit_write_protect_np);
DSYM("_pthread_set_qos_class_self_np", d_qos_zero);
DSYM("_pthread_attr_set_qos_class_np", d_qos_zero);
DSYM("_pthread_attr_get_qos_class_np", d_qos_zero);
DPASS(pthread_testcancel);
DPASS(pthread_atfork);
DPASS(sched_yield);

static int d_attr_init(struct dobj *a)
{
    a->sig = SIG_ATTR;
    return rc(pthread_attr_init(INNER(a, pthread_attr_t)));
}

static int d_attr_destroy(struct dobj *a)
{
    a->sig = 0;
    return 0;
}

static int d_attr_setdetachstate(struct dobj *a, int st)
{
    return rc(pthread_attr_setdetachstate(INNER(a, pthread_attr_t),
                                          st == 2 ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE));
}

static int d_attr_getdetachstate(const struct dobj *a, int *st)
{
    int s, e = pthread_attr_getdetachstate(INNER(a, pthread_attr_t), &s);
    *st = s == PTHREAD_CREATE_DETACHED ? 2 : 1;
    return rc(e);
}

static int d_attr_setstacksize(struct dobj *a, size_t n) { return rc(pthread_attr_setstacksize(INNER(a, pthread_attr_t), n)); }
static int d_attr_getstacksize(const struct dobj *a, size_t *n) { return rc(pthread_attr_getstacksize(INNER(a, pthread_attr_t), n)); }
static int d_attr_setstack(struct dobj *a, void *p, size_t n) { return rc(pthread_attr_setstack(INNER(a, pthread_attr_t), p, n)); }
static int d_attr_getstack(const struct dobj *a, void **p, size_t *n) { return rc(pthread_attr_getstack(INNER(a, pthread_attr_t), p, n)); }
static int d_attr_setguardsize(struct dobj *a, size_t n) { return rc(pthread_attr_setguardsize(INNER(a, pthread_attr_t), n)); }
static int d_attr_getguardsize(const struct dobj *a, size_t *n) { return rc(pthread_attr_getguardsize(INNER(a, pthread_attr_t), n)); }
static int d_attr_setscope(struct dobj *a, int s) { (void)a; return s == 1 ? 0 : 45; }

static int d_attr_getscope(const struct dobj *a, int *s)
{
    (void)a;
    *s = 1;
    return 0;
}

static int d_attr_setinheritsched(struct dobj *a, int v) { (void)a; (void)v; return 0; }

static int d_attr_getinheritsched(const struct dobj *a, int *v)
{
    (void)a;
    *v = 1;
    return 0;
}

static int d_attr_setschedpolicy(struct dobj *a, int p) { (void)a; (void)p; return 0; }

static int d_attr_getschedpolicy(const struct dobj *a, int *p)
{
    (void)a;
    *p = 1;
    return 0;
}

static int d_attr_setschedparam(struct dobj *a, const void *p) { (void)a; (void)p; return 0; }

static int d_attr_getschedparam(const struct dobj *a, int *p)
{
    (void)a;
    p[0] = 31;
    p[1] = 0;
    return 0;
}

DSYM("_pthread_attr_init", d_attr_init);
DSYM("_pthread_attr_destroy", d_attr_destroy);
DSYM("_pthread_attr_setdetachstate", d_attr_setdetachstate);
DSYM("_pthread_attr_getdetachstate", d_attr_getdetachstate);
DSYM("_pthread_attr_setstacksize", d_attr_setstacksize);
DSYM("_pthread_attr_getstacksize", d_attr_getstacksize);
DSYM("_pthread_attr_setstack", d_attr_setstack);
DSYM("_pthread_attr_getstack", d_attr_getstack);
DSYM("_pthread_attr_setguardsize", d_attr_setguardsize);
DSYM("_pthread_attr_getguardsize", d_attr_getguardsize);
DSYM("_pthread_attr_setscope", d_attr_setscope);
DSYM("_pthread_attr_getscope", d_attr_getscope);
DSYM("_pthread_attr_setinheritsched", d_attr_setinheritsched);
DSYM("_pthread_attr_getinheritsched", d_attr_getinheritsched);
DSYM("_pthread_attr_setschedpolicy", d_attr_setschedpolicy);
DSYM("_pthread_attr_getschedpolicy", d_attr_getschedpolicy);
DSYM("_pthread_attr_setschedparam", d_attr_setschedparam);
DSYM("_pthread_attr_getschedparam", d_attr_getschedparam);

static int mutex_type(int d)
{
    return d == 1 ? PTHREAD_MUTEX_ERRORCHECK : d == 2 ? PTHREAD_MUTEX_RECURSIVE : PTHREAD_MUTEX_NORMAL;
}

static int d_mutexattr_init(struct dobj *a)
{
    a->sig = SIG_MUTEXATTR;
    return rc(pthread_mutexattr_init(INNER(a, pthread_mutexattr_t)));
}

static int d_mutexattr_destroy(struct dobj *a)
{
    a->sig = 0;
    return 0;
}

static int d_mutexattr_settype(struct dobj *a, int t)
{
    if (t < 0 || t > 2)
        return 22;
    return rc(pthread_mutexattr_settype(INNER(a, pthread_mutexattr_t), mutex_type(t)));
}

static int d_mutexattr_gettype(const struct dobj *a, int *t)
{
    int v, e = pthread_mutexattr_gettype(INNER(a, pthread_mutexattr_t), &v);
    *t = v == PTHREAD_MUTEX_ERRORCHECK ? 1 : v == PTHREAD_MUTEX_RECURSIVE ? 2 : 0;
    return rc(e);
}

static int d_attr_setpshared(struct dobj *a, int v) { (void)a; return v == 1 || v == 2 ? 0 : 22; }

static int d_attr_getpshared(const struct dobj *a, int *v)
{
    (void)a;
    *v = 2;
    return 0;
}

static int d_mutexattr_noop(struct dobj *a, int v) { (void)a; (void)v; return 0; }

DSYM("_pthread_mutexattr_init", d_mutexattr_init);
DSYM("_pthread_mutexattr_destroy", d_mutexattr_destroy);
DSYM("_pthread_mutexattr_settype", d_mutexattr_settype);
DSYM("_pthread_mutexattr_gettype", d_mutexattr_gettype);
DSYM("_pthread_mutexattr_setpshared", d_attr_setpshared);
DSYM("_pthread_mutexattr_getpshared", d_attr_getpshared);
DSYM("_pthread_mutexattr_setpolicy_np", d_mutexattr_noop);
DSYM("_pthread_mutexattr_setprotocol", d_mutexattr_noop);
DSYM("_pthread_mutexattr_setprioceiling", d_mutexattr_noop);

static pthread_mutex_t *mtx(struct dobj *m)
{
    long s = __atomic_load_n(&m->sig, __ATOMIC_ACQUIRE);
    if (s == SIG_MUTEX_RECURSIVE || s == SIG_MUTEX_ERRCHK) {
        INNER(m, pthread_mutex_t)->type = s == SIG_MUTEX_RECURSIVE ? PTHREAD_MUTEX_RECURSIVE : PTHREAD_MUTEX_ERRORCHECK;
        __atomic_compare_exchange_n(&m->sig, &s, SIG_MUTEX, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }
    return INNER(m, pthread_mutex_t);
}

static int d_mutex_init(struct dobj *m, const struct dobj *a)
{
    m->sig = SIG_MUTEX;
    return rc(pthread_mutex_init(INNER(m, pthread_mutex_t), a ? INNER(a, pthread_mutexattr_t) : NULL));
}

static int d_mutex_destroy(struct dobj *m)
{
    int e = pthread_mutex_destroy(mtx(m));
    m->sig = 0;
    return rc(e);
}

static int d_mutex_lock(struct dobj *m) { return rc(pthread_mutex_lock(mtx(m))); }
static int d_mutex_trylock(struct dobj *m) { return rc(pthread_mutex_trylock(mtx(m))); }
static int d_mutex_unlock(struct dobj *m) { return rc(pthread_mutex_unlock(mtx(m))); }

DSYM("_pthread_mutex_init", d_mutex_init);
DSYM("_pthread_mutex_destroy", d_mutex_destroy);
DSYM("_pthread_mutex_lock", d_mutex_lock);
DSYM("_pthread_mutex_trylock", d_mutex_trylock);
DSYM("_pthread_mutex_unlock", d_mutex_unlock);

static int d_condattr_init(struct dobj *a)
{
    a->sig = SIG_CONDATTR;
    return 0;
}

static int d_condattr_destroy(struct dobj *a)
{
    a->sig = 0;
    return 0;
}

DSYM("_pthread_condattr_init", d_condattr_init);
DSYM("_pthread_condattr_destroy", d_condattr_destroy);
DSYM("_pthread_condattr_setpshared", d_attr_setpshared);
DSYM("_pthread_condattr_getpshared", d_attr_getpshared);

static int d_cond_init(struct dobj *c, const struct dobj *a)
{
    (void)a;
    c->sig = SIG_COND;
    return rc(pthread_cond_init(INNER(c, pthread_cond_t), NULL));
}

static int d_cond_destroy(struct dobj *c)
{
    int e = pthread_cond_destroy(INNER(c, pthread_cond_t));
    c->sig = 0;
    return rc(e);
}

static int d_cond_wait(struct dobj *c, struct dobj *m) { return rc(pthread_cond_wait(INNER(c, pthread_cond_t), mtx(m))); }

static int d_cond_timedwait(struct dobj *c, struct dobj *m, const struct timespec *ts)
{
    return rc(pthread_cond_timedwait(INNER(c, pthread_cond_t), mtx(m), ts));
}

static int d_cond_timedwait_relative_np(struct dobj *c, struct dobj *m, const struct timespec *rel)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += rel->tv_sec;
    ts.tv_nsec += rel->tv_nsec;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }
    return d_cond_timedwait(c, m, &ts);
}

static int d_cond_signal(struct dobj *c) { return rc(pthread_cond_signal(INNER(c, pthread_cond_t))); }
static int d_cond_broadcast(struct dobj *c) { return rc(pthread_cond_broadcast(INNER(c, pthread_cond_t))); }

DSYM("_pthread_cond_init", d_cond_init);
DSYM("_pthread_cond_destroy", d_cond_destroy);
DSYM("_pthread_cond_wait", d_cond_wait);
DSYM("_pthread_cond_timedwait", d_cond_timedwait);
DSYM("_pthread_cond_timedwait_relative_np", d_cond_timedwait_relative_np);
DSYM("_pthread_cond_signal", d_cond_signal);
DSYM("_pthread_cond_broadcast", d_cond_broadcast);

static int d_rwlockattr_init(struct dobj *a)
{
    a->sig = SIG_RWLOCKATTR;
    return 0;
}

static int d_rwlock_init(struct dobj *l, const struct dobj *a)
{
    (void)a;
    l->sig = SIG_RWLOCK;
    return rc(pthread_rwlock_init(INNER(l, pthread_rwlock_t), NULL));
}

static int d_rwlock_destroy(struct dobj *l)
{
    int e = pthread_rwlock_destroy(INNER(l, pthread_rwlock_t));
    l->sig = 0;
    return rc(e);
}

static int d_rwlock_rdlock(struct dobj *l) { return rc(pthread_rwlock_rdlock(INNER(l, pthread_rwlock_t))); }
static int d_rwlock_wrlock(struct dobj *l) { return rc(pthread_rwlock_wrlock(INNER(l, pthread_rwlock_t))); }
static int d_rwlock_tryrdlock(struct dobj *l) { return rc(pthread_rwlock_tryrdlock(INNER(l, pthread_rwlock_t))); }
static int d_rwlock_trywrlock(struct dobj *l) { return rc(pthread_rwlock_trywrlock(INNER(l, pthread_rwlock_t))); }
static int d_rwlock_unlock(struct dobj *l) { return rc(pthread_rwlock_unlock(INNER(l, pthread_rwlock_t))); }

DSYM("_pthread_rwlockattr_init", d_rwlockattr_init);
DSYM("_pthread_rwlockattr_destroy", d_condattr_destroy);
DSYM("_pthread_rwlockattr_setpshared", d_attr_setpshared);
DSYM("_pthread_rwlockattr_getpshared", d_attr_getpshared);
DSYM("_pthread_rwlock_init", d_rwlock_init);
DSYM("_pthread_rwlock_destroy", d_rwlock_destroy);
DSYM("_pthread_rwlock_rdlock", d_rwlock_rdlock);
DSYM("_pthread_rwlock_wrlock", d_rwlock_wrlock);
DSYM("_pthread_rwlock_tryrdlock", d_rwlock_tryrdlock);
DSYM("_pthread_rwlock_trywrlock", d_rwlock_trywrlock);
DSYM("_pthread_rwlock_unlock", d_rwlock_unlock);

static int d_once(struct dobj *o, void (*fn)(void)) { return rc(pthread_once(INNER(o, pthread_once_t), fn)); }
DSYM("_pthread_once", d_once);

static int d_key_create(unsigned long *k, void (*dtor)(void *))
{
    pthread_key_t key;
    int e = pthread_key_create(&key, dtor);
    if (!e)
        *k = key;
    return rc(e);
}

static int d_key_delete(unsigned long k) { return rc(pthread_key_delete((pthread_key_t)k)); }
static void *d_getspecific(unsigned long k) { return pthread_getspecific((pthread_key_t)k); }
static int d_setspecific(unsigned long k, const void *v) { return rc(pthread_setspecific((pthread_key_t)k, v)); }

DSYM("_pthread_key_create", d_key_create);
DSYM("_pthread_key_delete", d_key_delete);
DSYM("_pthread_getspecific", d_getspecific);
DSYM("_pthread_setspecific", d_setspecific);

static void d_unfair_lock(uint32_t *l)
{
    unsigned spins = 0;
    while (__atomic_exchange_n(l, 1u, __ATOMIC_ACQUIRE)) {
        if (++spins > 64)
            sched_yield();
        else
            __builtin_ia32_pause();
    }
}

static int d_unfair_trylock(uint32_t *l) { return !__atomic_exchange_n(l, 1u, __ATOMIC_ACQUIRE); }
static void d_unfair_unlock(uint32_t *l) { __atomic_store_n(l, 0u, __ATOMIC_RELEASE); }
static void d_unfair_assert(uint32_t *l) { (void)l; }

DSYM("_os_unfair_lock_lock", d_unfair_lock);
DSYM("_os_unfair_lock_lock_with_options", d_unfair_lock);
DSYM("_os_unfair_lock_trylock", d_unfair_trylock);
DSYM("_os_unfair_lock_unlock", d_unfair_unlock);
DSYM("_os_unfair_lock_assert_owner", d_unfair_assert);
DSYM("_os_unfair_lock_assert_not_owner", d_unfair_assert);
DSYM("_OSSpinLockLock", d_unfair_lock);
DSYM("_OSSpinLockTry", d_unfair_trylock);
DSYM("_OSSpinLockUnlock", d_unfair_unlock);

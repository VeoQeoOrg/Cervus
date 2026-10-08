#include <threads.h>
#include <errno.h>
#include <stdlib.h>
#include <sched.h>
#include <stdint.h>

struct start {
    thrd_start_t fn;
    void *arg;
};

static void *trampoline(void *p)
{
    struct start s = *(struct start *)p;
    free(p);
    return (void *)(intptr_t)s.fn(s.arg);
}

int thrd_create(thrd_t *thr, thrd_start_t func, void *arg)
{
    struct start *s = malloc(sizeof *s);
    if (!s) return thrd_nomem;
    s->fn = func;
    s->arg = arg;
    int r = pthread_create(thr, NULL, trampoline, s);
    if (r) {
        free(s);
        return r == EAGAIN ? thrd_nomem : thrd_error;
    }
    return thrd_success;
}

thrd_t thrd_current(void) { return pthread_self(); }
int thrd_detach(thrd_t thr) { return pthread_detach(thr) ? thrd_error : thrd_success; }
int thrd_equal(thrd_t a, thrd_t b) { return pthread_equal(a, b); }
void thrd_exit(int res) { pthread_exit((void *)(intptr_t)res); }

int thrd_join(thrd_t thr, int *res)
{
    void *r;
    if (pthread_join(thr, &r)) return thrd_error;
    if (res) *res = (int)(intptr_t)r;
    return thrd_success;
}

int thrd_sleep(const struct timespec *duration, struct timespec *remaining)
{
    if (nanosleep(duration, remaining) == 0) return 0;
    return errno == EINTR ? -1 : -2;
}

void thrd_yield(void) { sched_yield(); }

int mtx_init(mtx_t *mtx, int type)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    if (type & mtx_recursive) pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    int r = pthread_mutex_init(mtx, &a);
    pthread_mutexattr_destroy(&a);
    return r ? thrd_error : thrd_success;
}

void mtx_destroy(mtx_t *mtx) { pthread_mutex_destroy(mtx); }
int mtx_lock(mtx_t *mtx) { return pthread_mutex_lock(mtx) ? thrd_error : thrd_success; }

int mtx_timedlock(mtx_t *restrict mtx, const struct timespec *restrict ts)
{
    int r = pthread_mutex_timedlock(mtx, ts);
    return r == 0 ? thrd_success : r == ETIMEDOUT ? thrd_timedout : thrd_error;
}

int mtx_trylock(mtx_t *mtx)
{
    int r = pthread_mutex_trylock(mtx);
    return r == 0 ? thrd_success : r == EBUSY ? thrd_busy : thrd_error;
}

int mtx_unlock(mtx_t *mtx) { return pthread_mutex_unlock(mtx) ? thrd_error : thrd_success; }

void call_once(once_flag *flag, void (*func)(void)) { pthread_once(flag, func); }

int cnd_init(cnd_t *cond) { return pthread_cond_init(cond, NULL) ? thrd_error : thrd_success; }
void cnd_destroy(cnd_t *cond) { pthread_cond_destroy(cond); }
int cnd_signal(cnd_t *cond) { return pthread_cond_signal(cond) ? thrd_error : thrd_success; }
int cnd_broadcast(cnd_t *cond) { return pthread_cond_broadcast(cond) ? thrd_error : thrd_success; }
int cnd_wait(cnd_t *cond, mtx_t *mtx) { return pthread_cond_wait(cond, mtx) ? thrd_error : thrd_success; }

int cnd_timedwait(cnd_t *restrict cond, mtx_t *restrict mtx, const struct timespec *restrict ts)
{
    int r = pthread_cond_timedwait(cond, mtx, ts);
    return r == 0 ? thrd_success : r == ETIMEDOUT ? thrd_timedout : thrd_error;
}

int tss_create(tss_t *key, tss_dtor_t dtor) { return pthread_key_create(key, dtor) ? thrd_error : thrd_success; }
void tss_delete(tss_t key) { pthread_key_delete(key); }
int tss_set(tss_t key, void *val) { return pthread_setspecific(key, val) ? thrd_error : thrd_success; }
void *tss_get(tss_t key) { return pthread_getspecific(key); }

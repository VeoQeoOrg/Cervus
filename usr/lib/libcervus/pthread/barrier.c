#include <pthread.h>
#include <errno.h>

int pthread_barrierattr_init(pthread_barrierattr_t *a)
{
    if (!a) return EINVAL;
    a->pshared = PTHREAD_PROCESS_PRIVATE;
    return 0;
}

int pthread_barrierattr_destroy(pthread_barrierattr_t *a)
{
    return a ? 0 : EINVAL;
}

int pthread_barrierattr_setpshared(pthread_barrierattr_t *a, int pshared)
{
    if (!a || (pshared != PTHREAD_PROCESS_PRIVATE && pshared != PTHREAD_PROCESS_SHARED)) return EINVAL;
    a->pshared = pshared;
    return 0;
}

int pthread_barrierattr_getpshared(const pthread_barrierattr_t *restrict a, int *restrict pshared)
{
    if (!a || !pshared) return EINVAL;
    *pshared = a->pshared;
    return 0;
}

int pthread_barrier_init(pthread_barrier_t *restrict b, const pthread_barrierattr_t *restrict a, unsigned count)
{
    (void)a;
    if (!b || count == 0) return EINVAL;
    pthread_mutex_init(&b->m, NULL);
    pthread_cond_init(&b->c, NULL);
    b->count = count;
    b->waiting = 0;
    b->cycle = 0;
    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *b)
{
    if (!b) return EINVAL;
    pthread_mutex_lock(&b->m);
    int busy = b->waiting != 0;
    pthread_mutex_unlock(&b->m);
    if (busy) return EBUSY;
    pthread_cond_destroy(&b->c);
    pthread_mutex_destroy(&b->m);
    return 0;
}

int pthread_barrier_wait(pthread_barrier_t *b)
{
    if (!b) return EINVAL;
    pthread_mutex_lock(&b->m);
    unsigned cycle = b->cycle;
    if (++b->waiting == b->count) {
        b->waiting = 0;
        b->cycle++;
        pthread_cond_broadcast(&b->c);
        pthread_mutex_unlock(&b->m);
        return PTHREAD_BARRIER_SERIAL_THREAD;
    }
    while (cycle == b->cycle) pthread_cond_wait(&b->c, &b->m);
    pthread_mutex_unlock(&b->m);
    return 0;
}

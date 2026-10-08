#include <pthread.h>
#include <errno.h>

int pthread_spin_init(pthread_spinlock_t *l, int pshared)
{
    (void)pshared;
    if (!l) return EINVAL;
    *l = 0;
    return 0;
}

int pthread_spin_destroy(pthread_spinlock_t *l)
{
    return l ? 0 : EINVAL;
}

int pthread_spin_lock(pthread_spinlock_t *l)
{
    if (!l) return EINVAL;
    while (__atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE))
        while (__atomic_load_n(l, __ATOMIC_RELAXED)) __builtin_ia32_pause();
    return 0;
}

int pthread_spin_trylock(pthread_spinlock_t *l)
{
    if (!l) return EINVAL;
    return __atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE) ? EBUSY : 0;
}

int pthread_spin_unlock(pthread_spinlock_t *l)
{
    if (!l) return EINVAL;
    __atomic_store_n(l, 0, __ATOMIC_RELEASE);
    return 0;
}

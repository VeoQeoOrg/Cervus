#include <libcervus.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <time.h>
#include <sys/syscall.h>

static long sem_futex_wait(volatile int *addr, int expected, uint64_t timeout_ns)
{
    return (long)syscall3(SYS_FUTEX_WAIT, (uint64_t)(uintptr_t)addr, (uint64_t)(unsigned)expected, timeout_ns);
}

static void sem_futex_wake(volatile int *addr, int n)
{
    syscall2(SYS_FUTEX_WAKE, (uint64_t)(uintptr_t)addr, (uint64_t)n);
}

int sem_init(sem_t *sem, int pshared, unsigned value)
{
    if (value > SEM_VALUE_MAX) { errno = EINVAL; return -1; }
    sem->__value = (int)value;
    sem->__waiters = 0;
    sem->__pshared = pshared;
    sem->__pad = 0;
    return 0;
}

int sem_destroy(sem_t *sem)
{
    (void)sem;
    return 0;
}

int sem_post(sem_t *sem)
{
    int v = __atomic_load_n(&sem->__value, __ATOMIC_RELAXED);
    do {
        if (v == SEM_VALUE_MAX) { errno = EOVERFLOW; return -1; }
    } while (!__atomic_compare_exchange_n(&sem->__value, &v, v + 1, 0,
                                          __ATOMIC_RELEASE, __ATOMIC_RELAXED));
    if (__atomic_load_n(&sem->__waiters, __ATOMIC_ACQUIRE))
        sem_futex_wake(&sem->__value, 1);
    return 0;
}

int sem_trywait(sem_t *sem)
{
    int v = __atomic_load_n(&sem->__value, __ATOMIC_RELAXED);
    while (v > 0) {
        if (__atomic_compare_exchange_n(&sem->__value, &v, v - 1, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return 0;
    }
    errno = EAGAIN;
    return -1;
}

static void sem_unwait(void *p)
{
    sem_t *sem = p;
    __atomic_sub_fetch(&sem->__waiters, 1, __ATOMIC_ACQ_REL);
}

static int sem_wait_ns(sem_t *sem, clockid_t clock, const struct timespec *abstime)
{
    for (;;) {
        if (sem_trywait(sem) == 0) return 0;
        uint64_t timeout = 0;
        if (abstime) {
            if (abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000L) { errno = EINVAL; return -1; }
            struct timespec now;
            clock_gettime(clock, &now);
            int64_t left = (int64_t)(abstime->tv_sec - now.tv_sec) * 1000000000LL +
                           (abstime->tv_nsec - now.tv_nsec);
            if (left <= 0) { errno = ETIMEDOUT; return -1; }
            timeout = (uint64_t)left;
        }
        __atomic_add_fetch(&sem->__waiters, 1, __ATOMIC_ACQ_REL);
        struct __pthread_cleanup cb;
        __pthread_cleanup_push(&cb, sem_unwait, sem);
        int cp = __cervus_cp_enter();
        long r = sem_futex_wait(&sem->__value, 0, timeout);
        __cervus_cp_leave(cp);
        __pthread_cleanup_pop(&cb, 1);
        if (r == -EINTR) { errno = EINTR; return -1; }
        if (r == -ETIMEDOUT && sem_trywait(sem) != 0) { errno = ETIMEDOUT; return -1; }
    }
}

int sem_wait(sem_t *sem)
{
    return sem_wait_ns(sem, CLOCK_REALTIME, 0);
}

int sem_timedwait(sem_t *sem, const struct timespec *abstime)
{
    return sem_wait_ns(sem, CLOCK_REALTIME, abstime);
}

int sem_clockwait(sem_t *sem, clockid_t clock, const struct timespec *abstime)
{
    return sem_wait_ns(sem, clock, abstime);
}

int sem_getvalue(sem_t *sem, int *value)
{
    *value = __atomic_load_n(&sem->__value, __ATOMIC_RELAXED);
    return 0;
}

sem_t *sem_open(const char *name, int oflag, ...)
{
    (void)name;
    (void)oflag;
    errno = ENOSYS;
    return SEM_FAILED;
}

int sem_close(sem_t *sem)
{
    (void)sem;
    errno = ENOSYS;
    return -1;
}

int sem_unlink(const char *name)
{
    (void)name;
    errno = ENOSYS;
    return -1;
}

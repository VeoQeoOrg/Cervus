#include <sys/futex.h>
#include <sys/syscall.h>
#include <errno.h>

int futex_wait(volatile uint32_t *addr, uint32_t expected, uint64_t timeout_ns)
{
    long r = (long)syscall3(SYS_FUTEX_WAIT, (uint64_t)(uintptr_t)addr,
                            (uint64_t)expected, timeout_ns);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}

int futex_wake(volatile uint32_t *addr, int count)
{
    long r = (long)syscall3(SYS_FUTEX_WAKE, (uint64_t)(uintptr_t)addr,
                            (uint64_t)count, 0);
    if (r < 0) { errno = (int)-r; return -1; }
    return (int)r;
}

void futex_lock(volatile uint32_t *lock)
{
    for (;;) {
        uint32_t expect = 0;
        if (__atomic_compare_exchange_n(lock, &expect, 1, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            return;
        uint32_t held = __atomic_exchange_n(lock, 2, __ATOMIC_ACQUIRE);
        if (held == 0) return;
        futex_wait(lock, 2, 0);
    }
}

void futex_unlock(volatile uint32_t *lock)
{
    if (__atomic_exchange_n(lock, 0, __ATOMIC_RELEASE) == 2)
        futex_wake(lock, 1);
}

int futex_trylock(volatile uint32_t *lock)
{
    uint32_t expect = 0;
    return __atomic_compare_exchange_n(lock, &expect, 1, 0,
                                       __ATOMIC_ACQUIRE, __ATOMIC_RELAXED) ? 0 : -1;
}

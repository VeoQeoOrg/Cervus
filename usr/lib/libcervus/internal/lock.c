#include <libcervus.h>
#include <sys/syscall.h>

static inline int cas(volatile int *p, int expect, int want)
{
    return __atomic_compare_exchange_n(p, &expect, want, 0,
                                       __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
}

void __cervus_lock(__cervus_lock_t *l)
{
    if (cas(&l->state, 0, 1)) return;

    for (;;) {
        int prev = __atomic_exchange_n(&l->state, 2, __ATOMIC_ACQUIRE);
        if (prev == 0) return;
        syscall3(SYS_FUTEX_WAIT, (uint64_t)(uintptr_t)&l->state, 2, 0);
    }
}

void __cervus_unlock(__cervus_lock_t *l)
{
    int prev = __atomic_exchange_n(&l->state, 0, __ATOMIC_RELEASE);
    if (prev == 2)
        syscall2(SYS_FUTEX_WAKE, (uint64_t)(uintptr_t)&l->state, 1);
}

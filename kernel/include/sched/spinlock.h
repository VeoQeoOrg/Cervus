#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

typedef struct {
    volatile uint32_t ticket;
    volatile uint32_t serving;
} spinlock_t;

#define SPINLOCK_INIT { .ticket = 0, .serving = 0 }

static inline void spinlock_acquire(spinlock_t* lock) {
    uint32_t my_ticket = __atomic_fetch_add(&lock->ticket, 1, __ATOMIC_RELAXED);
    while (__atomic_load_n(&lock->serving, __ATOMIC_ACQUIRE) != my_ticket)
        asm volatile("pause");
}

static inline void spinlock_release(spinlock_t* lock) {
    __atomic_fetch_add(&lock->serving, 1, __ATOMIC_RELEASE);
}

static inline int spinlock_try_acquire(spinlock_t* lock) {
    uint32_t serving = __atomic_load_n(&lock->serving, __ATOMIC_RELAXED);
    uint32_t ticket  = __atomic_load_n(&lock->ticket,  __ATOMIC_RELAXED);
    if (serving != ticket) return 0;
    uint32_t expected = serving;
    return __atomic_compare_exchange_n(&lock->ticket, &expected, serving + 1,
                                       0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

#endif
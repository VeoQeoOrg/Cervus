#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

typedef struct {
    volatile uint32_t ticket;
    volatile uint32_t serving;
} spinlock_t;

#define SPINLOCK_INIT { .ticket = 0, .serving = 0 }

#define PERCPU_SPIN_DEPTH 184

void tlb_poll(void);

static inline void spin_depth_inc(void) {
    asm volatile("incl %%gs:%c0" :: "i"(PERCPU_SPIN_DEPTH) : "memory", "cc");
}

static inline void spin_depth_dec(void) {
    asm volatile("decl %%gs:%c0" :: "i"(PERCPU_SPIN_DEPTH) : "memory", "cc");
}

static inline int spin_depth(void) {
    int d;
    asm volatile("movl %%gs:%c1, %0" : "=r"(d) : "i"(PERCPU_SPIN_DEPTH));
    return d;
}

static inline void spinlock_acquire(spinlock_t* lock) {
    spin_depth_inc();
    uint32_t my_ticket = __atomic_fetch_add(&lock->ticket, 1, __ATOMIC_RELAXED);
    while (__atomic_load_n(&lock->serving, __ATOMIC_ACQUIRE) != my_ticket) {
        tlb_poll();
        asm volatile("pause");
    }
}

static inline void spinlock_release(spinlock_t* lock) {
    __atomic_fetch_add(&lock->serving, 1, __ATOMIC_RELEASE);
    spin_depth_dec();
}

static inline int spinlock_try_acquire(spinlock_t* lock) {
    uint32_t serving = __atomic_load_n(&lock->serving, __ATOMIC_RELAXED);
    uint32_t ticket  = __atomic_load_n(&lock->ticket,  __ATOMIC_RELAXED);
    if (serving != ticket) return 0;
    uint32_t expected = serving;
    spin_depth_inc();
    if (__atomic_compare_exchange_n(&lock->ticket, &expected, serving + 1,
                                    0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return 1;
    spin_depth_dec();
    return 0;
}

static inline uint64_t spinlock_acquire_irqsave(spinlock_t* lock) {
    uint64_t flags;
    asm volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    spinlock_acquire(lock);
    return flags;
}

static inline void spinlock_release_irqrestore(spinlock_t* lock, uint64_t flags) {
    spinlock_release(lock);
    asm volatile("push %0; popfq" :: "r"(flags) : "memory", "cc");
}

#endif
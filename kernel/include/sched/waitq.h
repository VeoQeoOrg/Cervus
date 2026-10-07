#ifndef SCHED_WAITQ_H
#define SCHED_WAITQ_H

#include <stdint.h>
#include "spinlock.h"

struct task;

#define WAITQ_SLOTS 32

typedef struct {
    struct task *slot[WAITQ_SLOTS];
} waitq_t;

void waitq_wake_all(waitq_t *q);
void waitq_block(waitq_t *q, spinlock_t *lock, uint64_t irqflags, uint64_t deadline_ns);

#endif

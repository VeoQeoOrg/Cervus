#ifndef SCHED_WAITQ_H
#define SCHED_WAITQ_H

#include <stdint.h>
#include "spinlock.h"

struct task;
struct waitq_cb;

#define WAITQ_SLOTS   32
#define WAITQ_POLLERS 32
#define WAITQ_CBS     8

typedef struct waitq_cb {
    void (*fn)(struct waitq_cb *cb);
} waitq_cb_t;

typedef struct {
    struct task     *slot[WAITQ_SLOTS];
    spinlock_t       plock;
    struct task     *poller[WAITQ_POLLERS];
    struct waitq_cb *cb[WAITQ_CBS];
} waitq_t;

void waitq_wake_all(waitq_t *q);
void waitq_wake_pollers(waitq_t *q);
void waitq_block(waitq_t *q, spinlock_t *lock, uint64_t irqflags, uint64_t deadline_ns);
int  waitq_poll_add(waitq_t *q, struct task *t);
void waitq_poll_del(waitq_t *q, struct task *t);
int  waitq_cb_add(waitq_t *q, waitq_cb_t *cb);
void waitq_cb_del(waitq_t *q, waitq_cb_t *cb);

#endif

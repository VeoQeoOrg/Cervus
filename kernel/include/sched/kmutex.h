#ifndef KMUTEX_H
#define KMUTEX_H

#include <stdbool.h>
#include "spinlock.h"

struct task;
struct kmutex_waiter;

typedef struct {
    volatile int          locked;
    struct task          *owner;
    spinlock_t            wait_lock;
    struct kmutex_waiter *head;
    struct kmutex_waiter *tail;
} kmutex_t;

#define KMUTEX_INIT { 0, 0, SPINLOCK_INIT, 0, 0 }

void kmutex_init(kmutex_t *m);
void kmutex_lock(kmutex_t *m);
bool kmutex_trylock(kmutex_t *m);
void kmutex_unlock(kmutex_t *m);
bool kmutex_held_by_me(kmutex_t *m);

#endif

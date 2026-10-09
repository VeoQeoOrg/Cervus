#ifndef CERVUS_KTIMER_H
#define CERVUS_KTIMER_H

#include <stdint.h>
#include <stdbool.h>

#define KTIMER_MONOTONIC 0
#define KTIMER_REALTIME  1

typedef struct ktimer ktimer_t;

struct ktimer {
    ktimer_t        *next;
    uint64_t         expires;
    int64_t          rt_expires;
    int              base;
    volatile int     queued;
    volatile int     running;
    void           (*fn)(ktimer_t *);
};

void     ktimer_init(ktimer_t *t, void (*fn)(ktimer_t *));
void     ktimer_arm(ktimer_t *t, uint64_t expires_mono_ns);
void     ktimer_arm_realtime(ktimer_t *t, int64_t expires_real_ns);
bool     ktimer_cancel(ktimer_t *t);
void     ktimer_run(uint64_t now_ns);
void     ktimer_clock_was_set(void);
int64_t  ktimer_realtime_offset(void);

#endif

#ifndef SCHED_SPEC_H
#define SCHED_SPEC_H

#include <stdint.h>
#include <stddef.h>
#include "spinlock.h"

struct task;

typedef void (*spec_fn_t)(void *shadow, size_t len, void *arg);

typedef enum {
    SPEC_IDLE = 0,
    SPEC_RUNNING,
    SPEC_DONE,
    SPEC_COMMITTED,
    SPEC_ABORTED,
} spec_state_t;

typedef struct spec_region {
    uint8_t     *master;
    uint8_t     *shadow;
    uint8_t     *base;
    size_t       len;
    size_t       npages;

    spec_fn_t    fn;
    void        *arg;
    struct task *worker;
    int          target_cpu;

    volatile int done;
    spec_state_t state;

    uint64_t     worker_dirty;
    uint64_t     main_dirty;
    uint64_t     conflicts;
    uint64_t     raw_conflicts;
    uint64_t     merged;

    size_t       read_lo;
    size_t       read_hi;

    spinlock_t   lock;
} spec_region_t;

spec_region_t *spec_region_create(void *master, size_t len);
void           spec_region_destroy(spec_region_t *r);

int  spec_run(spec_region_t *r, spec_fn_t fn, void *arg);
int  spec_run_ex(spec_region_t *r, spec_fn_t fn, void *arg,
                 size_t read_lo, size_t read_hi);
int  spec_wait(spec_region_t *r, uint32_t timeout_ms);
int  spec_poll(spec_region_t *r);
int  spec_commit(spec_region_t *r);
int  spec_abort(spec_region_t *r);

typedef void (*spec_range_fn_t)(void *data, size_t n, size_t base_off, void *arg);
int  spec_parallel_for(void *master, size_t len, int nways,
                       spec_range_fn_t fn, void *arg);

int  spec_selftest2(void);

#endif

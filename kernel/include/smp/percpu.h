#ifndef PERCPU_H
#define PERCPU_H

#include <stdint.h>
#include "../include/smp/smp.h"

#define PERCPU_SECTION __attribute__((section(".percpu")))

extern uintptr_t __percpu_start;
extern uintptr_t __percpu_end;
typedef struct {
    uint32_t cpu_id;
    void* current_task;
    uint64_t some_counter;
    bool need_resched;
} __attribute__((aligned(64))) percpu_t;

extern percpu_t percpu;
extern percpu_t* percpu_regions[MAX_CPUS];

percpu_t* get_percpu(void);
percpu_t* get_percpu_mut(void);
void init_percpu_regions(void);
void set_percpu_base(percpu_t* base);
#define current_cpu_id() (get_percpu()->cpu_id)

#endif
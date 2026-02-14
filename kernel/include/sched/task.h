#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_PRIORITY     31
#define DEFAULT_PRIORITY 16
#define TASK_DEFAULT_TIMESLICE 10

typedef struct {
    uint8_t data[512] __attribute__((aligned(16)));
} fpu_state_t;

typedef struct task {
    uint64_t rsp;
    uint64_t rip;
    uint64_t rbp;
    uint64_t cr3;
    int priority;
    bool runnable;
    uint32_t cpu_id;
    char name[32];

    uint32_t time_slice;
    uint32_t time_slice_init;
    uint64_t total_runtime;

    fpu_state_t* fpu_state;
    bool fpu_used;

    uint32_t last_cpu;
    uint64_t cpu_affinity;

    struct task* next;
} task_t;

extern task_t* ready_queues[MAX_PRIORITY + 1];
extern task_t* current_task[8];

void sched_init(void);
task_t* task_create(const char* name, void (*entry)(void*), void* arg, int priority);
void task_yield(void);
void sched_reschedule(void);
void sched_print_stats(void);

extern void context_switch(task_t* old, task_t* next);
extern void first_task_start(task_t* task);

extern void fpu_save(fpu_state_t* state);
extern void fpu_restore(fpu_state_t* state);

#endif
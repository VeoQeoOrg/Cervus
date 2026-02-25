#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_PRIORITY     31
#define DEFAULT_PRIORITY 16
#define TASK_DEFAULT_TIMESLICE 10

typedef struct {
    uint8_t data[512] __attribute__((aligned(16)));
} fpu_state_t;

typedef enum {
    TASK_RUNNING = 0,
    TASK_READY,
    TASK_ZOMBIE,
    TASK_DEAD,
} task_state_t;

typedef struct task {
    uint64_t rsp;
    uint64_t rip;
    uint64_t rbp_save;
    uint64_t cr3;
    int      priority;
    bool     runnable;
    uint8_t  _pad0[3];
    uint32_t cpu_id;
    char     name[32];

    uint32_t time_slice;
    uint32_t time_slice_init;
    uint8_t  _pad1[4];
    uint64_t total_runtime;

    fpu_state_t* fpu_state;
    bool     fpu_used;
    uint8_t  _pad2[3];
    uint32_t last_cpu;
    uint64_t cpu_affinity;

    void (*entry)(void*);
    void *arg;

    uintptr_t stack_base;

    task_state_t state;

    struct task* next;
} task_t;

_Static_assert(offsetof(task_t, rsp)   ==   0, "task_t: rsp offset changed");
_Static_assert(offsetof(task_t, entry) == 120, "task_t: entry offset changed — update TASK_ENTRY_OFFSET in task_trampoline.asm");
_Static_assert(offsetof(task_t, arg)   == 128, "task_t: arg offset changed   — update TASK_ARG_OFFSET in task_trampoline.asm");

extern task_t* ready_queues[MAX_PRIORITY + 1];
extern task_t* current_task[8];

void sched_init(void);
task_t* task_create(const char* name, void (*entry)(void*), void* arg, int priority);
void task_yield(void);
void sched_reschedule(void);
void sched_print_stats(void);

__attribute__((noreturn)) void task_exit(void);

void task_kill(task_t* task);

void task_destroy(task_t* task);

extern void context_switch(task_t* old, task_t* next);
extern void first_task_start(task_t* task);
extern void task_trampoline(void);

extern void fpu_save(fpu_state_t* state);
extern void fpu_restore(fpu_state_t* state);

#endif
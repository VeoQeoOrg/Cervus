#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "capabilities.h"
#include "../memory/vmm.h"

typedef struct {
    uint8_t data[512] __attribute__((aligned(16)));
} fpu_state_t;

typedef enum {
    TASK_RUNNING = 0,
    TASK_READY,
    TASK_BLOCKED,
    TASK_ZOMBIE,
    TASK_DEAD,
} task_state_t;

typedef enum {
    TASK_TYPE_KERNEL    = 0,
    TASK_TYPE_USER      = 1,
} task_type_t;

#define MAX_PRIORITY           31
#define DEFAULT_PRIORITY       16
#define TASK_DEFAULT_TIMESLICE 10

typedef struct task {
    uint64_t rsp;
    uint64_t rip;
    uint64_t rbp_save;
    uint64_t cr3;
    int priority;
    bool runnable;
    uint8_t is_userspace;
    uint8_t _pad0[2];
    uint32_t cpu_id;
    char name[32];

    uint32_t time_slice;
    uint32_t time_slice_init;
    uint8_t _pad1[4];
    uint64_t total_runtime;

    fpu_state_t* fpu_state;
    bool fpu_used;
    uint8_t _pad2[3];
    uint32_t last_cpu;
    uint64_t cpu_affinity;

    void (*entry)(void*);
    void *arg;

    uintptr_t stack_base;
    uint64_t user_rsp;

    task_state_t state;
    uint8_t _pad3[4];

    struct task* next;

    uint32_t pid;
    uint32_t ppid;
    uint32_t uid;
    uint32_t gid;
    uint64_t capabilities;

    struct task* parent;
    struct task* children;
    struct task* sibling;

    int exit_code;

    uintptr_t brk_start;
    uintptr_t brk_current;
    uintptr_t brk_max;
    vmm_pagemap_t* pagemap;

    uint32_t flags;

    uint32_t wait_for_pid;
    uint64_t user_saved_rip;
    uint64_t user_saved_rbp;
    uint64_t user_saved_rbx;
    uint64_t user_saved_r12;
    uint64_t user_saved_r13;
    uint64_t user_saved_r14;
    uint64_t user_saved_r15;
    uint64_t user_saved_r11;

} task_t;

#define TASK_FLAG_TRACE    (1 << 0)
#define TASK_FLAG_VFORK    (1 << 1)
#define TASK_FLAG_FORK     (1 << 2)
#define TASK_FLAG_STARTED  (1 << 3)

_Static_assert(offsetof(task_t, rsp)            ==   0, "task_t: rsp");
_Static_assert(offsetof(task_t, entry)          == 120, "task_t: entry — update TASK_ENTRY_OFFSET");
_Static_assert(offsetof(task_t, arg)            == 128, "task_t: arg   — update TASK_ARG_OFFSET");
_Static_assert(offsetof(task_t, stack_base)     == 136, "task_t: stack_base");
_Static_assert(offsetof(task_t, user_rsp)       == 144, "task_t: user_rsp — update TASK_USER_RSP_OFFSET");
_Static_assert(offsetof(task_t, user_saved_rip) == 264, "task_t: user_saved_rip");
_Static_assert(offsetof(task_t, user_saved_rbp) == 272, "task_t: user_saved_rbp — update TASK_USER_SAVED_RBP_OFFSET");

extern task_t* ready_queues[MAX_PRIORITY + 1];
extern task_t* current_task[8];

void sched_init(void);
void sched_reschedule(void);
void sched_print_stats(void);
void task_yield(void);

task_t* task_create(const char* name, void (*entry)(void*), void* arg, int priority);

task_t* task_create_user(const char* name, uintptr_t entry, uintptr_t user_rsp, uint64_t cr3, int priority, vmm_pagemap_t* pagemap, uint32_t uid, uint32_t gid);

__attribute__((noreturn)) void task_exit(void);
void    task_kill(task_t* task);
void    task_destroy(task_t* task);
task_t* task_fork(task_t* parent);
task_t* task_find_by_pid(uint32_t pid);
uint32_t task_alloc_pid(void);
void    task_reparent(task_t* child, task_t* new_parent);
void    task_wakeup_waiters(uint32_t pid);

extern void context_switch(task_t* old, task_t* next);
extern void first_task_start(task_t* task);
extern void task_trampoline(void);
extern void task_trampoline_user(void);
extern void task_trampoline_fork(void);
extern void fpu_save(fpu_state_t* state);
extern void fpu_restore(fpu_state_t* state);

#endif
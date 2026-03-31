#ifndef PERCPU_H
#define PERCPU_H

#include <stdint.h>
#include "../include/smp/smp.h"

#define PERCPU_SECTION __attribute__((section(".percpu")))

typedef struct {
    uint64_t syscall_kernel_rsp;
    uint64_t syscall_user_rsp;
    uint32_t cpu_id;
    uint32_t _pad;
    void* current_task;
    uint64_t some_counter;
    bool need_resched;
    uint8_t _pad2[7];
    uint64_t user_saved_rbp;
    uint64_t user_saved_rbx;

    uint64_t user_saved_r12;
    uint64_t user_saved_r13;
    uint64_t user_saved_r14;
    uint64_t user_saved_r15;
    uint64_t user_saved_r11;
    uint64_t user_saved_rip;
    uint8_t  _pad3[8];
    void* deferred_free_task;
    uint64_t sched_stack_top;
} __attribute__((aligned(64))) percpu_t;

_Static_assert(__builtin_offsetof(percpu_t, syscall_kernel_rsp) == 0, "percpu: kernel_rsp");
_Static_assert(__builtin_offsetof(percpu_t, syscall_user_rsp) == 8, "percpu: user_rsp");
_Static_assert(__builtin_offsetof(percpu_t, current_task) == 24, "percpu: current_task");
_Static_assert(__builtin_offsetof(percpu_t, need_resched) == 40, "percpu: need_resched");
_Static_assert(__builtin_offsetof(percpu_t, user_saved_rbp) == 48, "percpu: saved_rbp");
_Static_assert(__builtin_offsetof(percpu_t, user_saved_rbx) == 56, "percpu: saved_rbx");
_Static_assert(__builtin_offsetof(percpu_t, user_saved_r12) == 64, "percpu: saved_r12");
_Static_assert(__builtin_offsetof(percpu_t, user_saved_r13) == 72, "percpu: saved_r13");
_Static_assert(__builtin_offsetof(percpu_t, user_saved_r14) == 80, "percpu: saved_r14");
_Static_assert(__builtin_offsetof(percpu_t, user_saved_r15) == 88, "percpu: saved_r15");
_Static_assert(__builtin_offsetof(percpu_t, user_saved_r11) == 96, "percpu: saved_r11");
_Static_assert(__builtin_offsetof(percpu_t, user_saved_rip) == 104, "percpu: saved_rip");

extern percpu_t percpu;
extern percpu_t* percpu_regions[MAX_CPUS];

percpu_t* get_percpu(void);
percpu_t* get_percpu_mut(void);
void init_percpu_regions(void);
void set_percpu_base(percpu_t* base);
#define current_cpu_id() (get_percpu()->cpu_id)

#endif

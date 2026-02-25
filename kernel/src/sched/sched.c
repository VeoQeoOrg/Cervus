#include "../include/sched/task.h"
#include "../include/smp/smp.h"
#include "../include/apic/apic.h"
#include "../include/io/serial.h"
#include "../include/memory/pmm.h"
#include "../include/memory/vmm.h"
#include "../include/drivers/timer.h"
#include <string.h>
#include <stdlib.h>

task_t* ready_queues[MAX_PRIORITY + 1] = {0};
task_t* current_task[8] = {0};

static task_t* percpu_ready_queues[8][MAX_PRIORITY + 1] = {0};
static task_t  idle_tasks[8];
static volatile uint64_t reschedule_calls = 0;
static volatile uint32_t global_queue_lock = 0;

void idle_loop(void* arg) {
    (void)arg;
    while (1) {
        asm volatile ("hlt");
    }
}

static inline void spin_lock(volatile uint32_t* lock) {
    while (__sync_lock_test_and_set(lock, 1))
        asm volatile ("pause");
}

static inline void spin_unlock(volatile uint32_t* lock) {
    __sync_lock_release(lock);
}

static uint64_t alloc_and_init_stack(task_t* t) {
    uintptr_t stack_virt = (uintptr_t)pmm_alloc(4);
    if (!stack_virt) return 0;

    uintptr_t stack_phys = pmm_virt_to_phys((void*)stack_virt);
    for (size_t i = 0; i < 4; i++) {
        vmm_map_page(vmm_get_kernel_pagemap(),
                     stack_virt + i * 0x1000,
                     stack_phys + i * 0x1000,
                     VMM_PRESENT | VMM_WRITE | VMM_GLOBAL);
    }
    asm volatile ("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");

    uintptr_t stack_top = (stack_virt + 0x4000) & ~0xFULL;
    uint64_t* sp = (uint64_t*)stack_top;

    extern void task_trampoline(void);

    *--sp = (uint64_t)task_trampoline;
    *--sp = (uint64_t)t;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;
    *--sp = 0;

    return (uint64_t)sp;
}

void sched_init(void) {
    for (uint32_t i = 0; i < smp_get_cpu_count(); i++) {
        task_t* idle = &idle_tasks[i];
        memset(idle, 0, sizeof(task_t));
        idle->priority       = 0;
        idle->runnable       = true;
        idle->cpu_id         = i;
        idle->last_cpu       = i;
        idle->rip            = (uint64_t)idle_loop;
        idle->time_slice     = 1;
        idle->time_slice_init= 1;
        idle->fpu_used       = false;
        idle->fpu_state      = NULL;
        idle->entry          = idle_loop;
        idle->arg            = NULL;
        idle->name[0]='i'; idle->name[1]='d'; idle->name[2]='l'; idle->name[3]='e';

        idle->rsp = alloc_and_init_stack(idle);
        if (!idle->rsp) {
            serial_printf("[SCHED] FATAL: cannot alloc idle stack for CPU %u\n", i);
            while (1) asm volatile("cli; hlt");
        }

        current_task[i] = NULL;
        for (int p = 0; p <= MAX_PRIORITY; p++)
            percpu_ready_queues[i][p] = NULL;
    }
    serial_writestring("Scheduler initialized (PREEMPTIVE SMP MODE)\n");
}

task_t* task_create(const char* name, void (*entry)(void*), void* arg, int priority) {
    task_t* t = calloc(1, sizeof(task_t));
    if (!t) return NULL;

    t->entry          = entry;
    t->arg            = arg;
    t->priority       = priority > MAX_PRIORITY ? MAX_PRIORITY : priority;
    t->runnable       = true;
    t->cpu_id         = (uint32_t)-1;
    t->last_cpu       = 0;
    t->cpu_affinity   = 0;
    t->time_slice     = TASK_DEFAULT_TIMESLICE;
    t->time_slice_init= TASK_DEFAULT_TIMESLICE;
    t->total_runtime  = 0;
    t->rip            = (uint64_t)entry;

    t->rsp = alloc_and_init_stack(t);
    if (!t->rsp) {
        free(t);
        return NULL;
    }

    t->fpu_state = (fpu_state_t*)aligned_alloc(16, sizeof(fpu_state_t));
    if (!t->fpu_state) {
        serial_printf("WARNING: Failed to allocate FPU state for task %s\n", name);
        t->fpu_used = false;
    } else {
        memset(t->fpu_state, 0, sizeof(fpu_state_t));
        *(uint16_t*)(t->fpu_state->data)      = 0x037F;
        *(uint32_t*)(t->fpu_state->data + 24) = 0x1F80;
        t->fpu_used = false;
        serial_printf("Task %s: FPU state initialized (FCW=0x%x, MXCSR=0x%x)\n",
                      name,
                      *(uint16_t*)(t->fpu_state->data),
                      *(uint32_t*)(t->fpu_state->data + 24));
    }

    strncpy(t->name, name, 31);

    spin_lock(&global_queue_lock);
    t->next = ready_queues[t->priority];
    ready_queues[t->priority] = t;
    spin_unlock(&global_queue_lock);

    serial_printf("Task created: %s (prio %d, timeslice %u, rsp=0x%llx, FPU=%s)\n",
                  name, t->priority, t->time_slice, t->rsp,
                  t->fpu_state ? "yes" : "no");
    return t;
}

static task_t* sched_pick_next(uint32_t cpu) {
    for (int p = MAX_PRIORITY; p >= 0; p--) {
        if (percpu_ready_queues[cpu][p]) {
            task_t* t = percpu_ready_queues[cpu][p];
            percpu_ready_queues[cpu][p] = t->next;
            t->next = NULL;
            return t;
        }
    }
    spin_lock(&global_queue_lock);
    for (int p = MAX_PRIORITY; p >= 0; p--) {
        if (ready_queues[p]) {
            task_t* t = ready_queues[p];
            ready_queues[p] = t->next;
            t->next = NULL;
            spin_unlock(&global_queue_lock);
            return t;
        }
    }
    spin_unlock(&global_queue_lock);
    return &idle_tasks[cpu];
}

void sched_reschedule(void) {
    reschedule_calls++;

    uint32_t cpu  = lapic_get_id();
    task_t*  old  = current_task[cpu];
    task_t*  next = sched_pick_next(cpu);

    if (old == next) {
        if (old) old->time_slice = old->time_slice_init;
        return;
    }

    if (old && old->fpu_state) {
        fpu_save(old->fpu_state);
        old->fpu_used = true;
    }

    if (old && old != &idle_tasks[cpu] && old->runnable) {
        old->time_slice = old->time_slice_init;
        old->last_cpu   = cpu;
        old->next = percpu_ready_queues[cpu][old->priority];
        percpu_ready_queues[cpu][old->priority] = old;
    }

    if (next->cr3 != 0 && (!old || old->cr3 != next->cr3))
        asm volatile ("mov %0, %%cr3" :: "r"(next->cr3) : "memory");

    next->cpu_id    = cpu;
    current_task[cpu] = next;

    if (next->fpu_state)
        fpu_restore(next->fpu_state);

    if (!old) {
        asm volatile ("" ::: "memory");
        first_task_start(next);
        serial_writestring("[SCHED] FATAL: first_task_start returned!\n");
        while (1) asm volatile ("cli; hlt");
    }

    context_switch(old, next);
}

void task_yield(void) {
    sched_reschedule();
}

void sched_print_stats(void) {
    serial_printf("\nScheduler Statistics (SMP)\n");
    serial_writestring("Global queue:\n");
    for (int p = MAX_PRIORITY; p >= 0; p--) {
        int count = 0;
        for (task_t* t = ready_queues[p]; t; t = t->next) count++;
        if (count) serial_printf("  Priority %d: %d tasks\n", p, count);
    }
    for (uint32_t cpu = 0; cpu < smp_get_cpu_count(); cpu++) {
        serial_printf("\nCPU %u:\n", cpu);
        if (current_task[cpu])
            serial_printf("  Running: %s (FPU: %s)\n",
                          current_task[cpu]->name,
                          current_task[cpu]->fpu_used ? "yes" : "no");
        for (int p = MAX_PRIORITY; p >= 0; p--) {
            int count = 0;
            for (task_t* t = percpu_ready_queues[cpu][p]; t; t = t->next) count++;
            if (count) serial_printf("  Priority %d: %d tasks\n", p, count);
        }
    }
    serial_printf("============================\n\n");
}
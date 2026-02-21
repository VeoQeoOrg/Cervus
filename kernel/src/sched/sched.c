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

static task_t idle_tasks[8];
static volatile uint64_t reschedule_calls = 0;

static volatile uint32_t global_queue_lock = 0;

void idle_loop(void* arg) {
    (void)arg;
    while (1) {
        asm volatile ("hlt");
    }
}

static inline void spin_lock(volatile uint32_t* lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        asm volatile ("pause");
    }
}

static inline void spin_unlock(volatile uint32_t* lock) {
    __sync_lock_release(lock);
}

void sched_init(void) {
    for (uint32_t i = 0; i < smp_get_cpu_count(); i++) {
        task_t* idle = &idle_tasks[i];
        memset(idle, 0, sizeof(task_t));
        idle->priority = 0;
        idle->runnable = true;
        idle->cpu_id = i;
        idle->last_cpu = i;
        idle->rip = (uint64_t)idle_loop;
        idle->time_slice = 1;
        idle->time_slice_init = 1;
        idle->total_runtime = 0;
        idle->fpu_used = false;
        idle->fpu_state = NULL;
        idle->name[0] = 'i'; idle->name[1] = 'd'; idle->name[2] = 'l'; idle->name[3] = 'e';

        current_task[i] = NULL;

        for (int p = 0; p <= MAX_PRIORITY; p++) {
            percpu_ready_queues[i][p] = NULL;
        }
    }

    serial_writestring("Scheduler initialized (PREEMPTIVE SMP MODE)\n");
}

task_t* task_create(const char* name, void (*entry)(void*), void* arg, int priority) {
    (void)arg;

    task_t* t = calloc(1, sizeof(task_t));
    if (!t) return NULL;

    uintptr_t stack_virt = (uintptr_t)pmm_alloc(4);
    if (!stack_virt) {
        free(t);
        return NULL;
    }

    uintptr_t stack_phys = pmm_virt_to_phys((void*)stack_virt);

    for (size_t i = 0; i < 4; i++) {
        uintptr_t v = stack_virt + i * 0x1000;
        uintptr_t p = stack_phys + i * 0x1000;
        vmm_map_page(vmm_get_kernel_pagemap(), v, p, VMM_PRESENT | VMM_WRITE | VMM_GLOBAL);
    }

    asm volatile ("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");

    uintptr_t stack_top = stack_virt + 0x4000;
    stack_top &= ~0xFULL;

    uint64_t* stack = (uint64_t*)stack_top;

    *--stack = (uint64_t)entry;
    *--stack = 0;  // r15
    *--stack = 0;  // r14
    *--stack = 0;  // r13
    *--stack = 0;  // r12
    *--stack = 0;  // rbx
    *--stack = 0;  // rbp

    t->rsp = (uint64_t)stack;
    t->rip = (uint64_t)entry;
    t->priority = priority > MAX_PRIORITY ? MAX_PRIORITY : priority;
    t->runnable = true;
    t->cpu_id = -1;
    t->last_cpu = 0;
    t->cpu_affinity = 0;
    t->time_slice = TASK_DEFAULT_TIMESLICE;
    t->time_slice_init = TASK_DEFAULT_TIMESLICE;
    t->total_runtime = 0;

    t->fpu_state = (fpu_state_t*)aligned_alloc(16, sizeof(fpu_state_t));
    if (!t->fpu_state) {
        serial_printf("WARNING: Failed to allocate FPU state for task %s\n", name);
        t->fpu_used = false;
    } else {
        memset(t->fpu_state, 0, sizeof(fpu_state_t));

        uint16_t* fcw = (uint16_t*)t->fpu_state->data;
        *fcw = 0x037F;

        uint32_t* mxcsr = (uint32_t*)(t->fpu_state->data + 24);
        *mxcsr = 0x1F80;

        t->fpu_used = false;

        serial_printf("Task %s: FPU state initialized (FCW=0x%x, MXCSR=0x%x)\n",
                     name, *fcw, *mxcsr);
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
    static uint64_t pick_calls = 0;
    pick_calls++;

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

            if (pick_calls <= 10) {
                serial_printf("[CPU %u] Migrated task %s from global queue\n",
                             cpu, t->name);
            }

            spin_unlock(&global_queue_lock);
            return t;
        }
    }
    spin_unlock(&global_queue_lock);

    return &idle_tasks[cpu];
}

void sched_reschedule(void) {
    static uint64_t first_call = 0;

    reschedule_calls++;

    if (first_call == 0) {
        serial_writestring("\n!!! FIRST SCHED_RESCHEDULE CALL !!!\n");
        first_call = 1;
    }

    uint32_t cpu = lapic_get_id();
    task_t* old = current_task[cpu];

    if (reschedule_calls <= 10) {
        serial_printf("[CPU %u] Reschedule #%llu: old task: %s\n",
                     cpu, reschedule_calls, old ? old->name : "NULL");
    }

    task_t* next = sched_pick_next(cpu);

    if (reschedule_calls <= 10) {
        serial_printf("[CPU %u]   Picked next: %s (fpu_used=%d)\n",
                     cpu, next ? next->name : "NULL", next ? next->fpu_used : -1);
    }

    if (old == next) {
        if (old) old->time_slice = old->time_slice_init;
        return;
    }

    if (old && old->fpu_state) {
        if (!old->fpu_used) {
            old->fpu_used = true;
            if (reschedule_calls <= 10) {
                serial_printf("[CPU %u] Task %s started using FPU\n",
                             cpu, old->name);
            }
        }

        fpu_save(old->fpu_state);

        if (reschedule_calls <= 10) {
            serial_printf("[CPU %u]   Saved FPU state for %s\n", cpu, old->name);
        }
    }

    if (old && old != &idle_tasks[cpu] && old->runnable) {
        old->last_cpu = cpu;
        old->next = percpu_ready_queues[cpu][old->priority];
        percpu_ready_queues[cpu][old->priority] = old;
    }

    if (next->cr3 != 0 && (!old || old->cr3 != next->cr3)) {
        asm volatile ("mov %0, %%cr3" :: "r"(next->cr3) : "memory");
    }

    current_task[cpu] = next;

    if (!old) {
        if (reschedule_calls <= 10) {
            serial_printf("[CPU %u]   First switch - calling first_task_start for %s\n",
                         cpu, next->name);
        }

        if (next->fpu_state) {
            fpu_restore(next->fpu_state);
            if (reschedule_calls <= 10) {
                serial_printf("[CPU %u]   Initialized FPU from fpu_state\n", cpu);
            }
        }

        asm volatile ("" ::: "memory");
        first_task_start(next);

        serial_writestring("[SCHED] FATAL: first_task_start returned!\n");
        while (1) {
            asm volatile ("cli; hlt");
        }
    }

    if (next->fpu_state && next->fpu_used) {
        fpu_restore(next->fpu_state);
        if (reschedule_calls <= 10) {
            serial_printf("[CPU %u]   Restored FPU state for %s\n", cpu, next->name);
        }
    }

    if (reschedule_calls <= 10) {
        serial_printf("[CPU %u]   Calling context_switch(0x%llx, 0x%llx)\n",
                     cpu, (uint64_t)old, (uint64_t)next);
    }

    context_switch(old, next);
}

void task_yield(void) {
    sched_reschedule();
}

void sched_print_stats(void) {
    serial_printf("\n=== Scheduler Statistics (SMP) ===\n");

    serial_writestring("Global queue:\n");
    for (int p = MAX_PRIORITY; p >= 0; p--) {
        task_t* t = ready_queues[p];
        int count = 0;
        while (t) {
            count++;
            t = t->next;
        }
        if (count > 0) {
            serial_printf("  Priority %d: %d tasks\n", p, count);
        }
    }

    for (uint32_t cpu = 0; cpu < smp_get_cpu_count(); cpu++) {
        serial_printf("\nCPU %u:\n", cpu);

        if (current_task[cpu]) {
            serial_printf("  Running: %s (FPU: %s)\n",
                         current_task[cpu]->name,
                         current_task[cpu]->fpu_used ? "yes" : "no");
        }

        for (int p = MAX_PRIORITY; p >= 0; p--) {
            task_t* t = percpu_ready_queues[cpu][p];
            int count = 0;
            while (t) {
                count++;
                t = t->next;
            }
            if (count > 0) {
                serial_printf("  Priority %d: %d tasks\n", p, count);
            }
        }
    }

    serial_printf("============================\n\n");
}
#include "../include/sched/sched.h"
#include "../include/memory/pmm.h"
#include "../include/memory/vmm.h"
#include "../include/io/serial.h"
#include "../include/smp/smp.h"
#include "../include/smp/percpu.h"
#include "../include/apic/apic.h"
#include "../include/gdt/gdt.h"
#include <string.h>
#include <stdlib.h>

task_t* ready_queues[MAX_PRIORITY + 1] = {0};
task_t* current_task[8] = {0};

static task_t* percpu_ready_queues[8][MAX_PRIORITY + 1] = {0};
static task_t  idle_tasks[8];
static volatile uint64_t reschedule_calls = 0;

#define KERNEL_STACK_PAGES (KERNEL_STACK_SIZE / 0x1000)

extern tss_t* tss[MAX_CPUS];

static void idle_loop(void* arg);

static uint64_t alloc_and_init_stack(task_t* t) {
    uintptr_t stack_virt = (uintptr_t)pmm_alloc(KERNEL_STACK_PAGES);
    if (!stack_virt) return 0;

    t->stack_base = stack_virt;

    uintptr_t stack_phys = pmm_virt_to_phys((void*)stack_virt);
    for (size_t i = 0; i < KERNEL_STACK_PAGES; i++) {
        vmm_map_page(vmm_get_kernel_pagemap(),
                     stack_virt + i * 0x1000,
                     stack_phys + i * 0x1000,
                     VMM_PRESENT | VMM_WRITE | VMM_GLOBAL);
    }
    asm volatile ("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");

    uintptr_t stack_top = (stack_virt + KERNEL_STACK_SIZE) & ~0xFULL;
    uint64_t* sp = (uint64_t*)stack_top;

    if (t->is_userspace) {
        extern void task_trampoline_user(void);
        *--sp = (uint64_t)task_trampoline_user;
    } else {
        extern void task_trampoline(void);
        *--sp = (uint64_t)task_trampoline;
    }

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
        idle->priority        = 0;
        idle->runnable        = true;
        idle->state           = TASK_READY;
        idle->cpu_id          = i;
        idle->last_cpu        = i;
        idle->rip             = (uint64_t)idle_loop;
        idle->time_slice      = 1;
        idle->time_slice_init = 1;
        idle->fpu_used        = false;
        idle->fpu_state       = NULL;
        idle->entry           = idle_loop;
        idle->arg             = NULL;
        idle->is_userspace    = 0;
        idle->user_rsp        = 0;
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

    t->entry           = entry;
    t->arg             = arg;
    t->priority        = priority > MAX_PRIORITY ? MAX_PRIORITY : priority;
    t->runnable        = true;
    t->state           = TASK_READY;
    t->cpu_id          = (uint32_t)-1;
    t->last_cpu        = 0;
    t->cpu_affinity    = 0;
    t->time_slice      = TASK_DEFAULT_TIMESLICE;
    t->time_slice_init = TASK_DEFAULT_TIMESLICE;
    t->total_runtime   = 0;
    t->rip             = (uint64_t)entry;
    t->is_userspace    = 0;
    t->user_rsp        = 0;

    strncpy(t->name, name, sizeof(t->name) - 1);

    t->rsp = alloc_and_init_stack(t);
    if (!t->rsp) { free(t); return NULL; }

    t->fpu_state = (fpu_state_t*)pmm_alloc_zero(1);
    t->next = ready_queues[t->priority];
    ready_queues[t->priority] = t;

    serial_printf("Task created: %s (prio %d, timeslice %d, rsp=0x%llx, FPU=%s)\n",
                  t->name, t->priority, t->time_slice,
                  t->rsp, t->fpu_state ? "yes" : "no");
    return t;
}

task_t* task_create_user(const char* name,
                          uintptr_t   entry,
                          uintptr_t   user_rsp,
                          uint64_t    cr3,
                          int         priority)
{
    task_t* t = calloc(1, sizeof(task_t));
    if (!t) return NULL;
    t->entry           = (void (*)(void*))entry;
    t->arg             = NULL;
    t->priority        = priority > MAX_PRIORITY ? MAX_PRIORITY : priority;
    t->runnable        = true;
    t->state           = TASK_READY;
    t->cpu_id          = (uint32_t)-1;
    t->last_cpu        = 0;
    t->cpu_affinity    = 0;
    t->time_slice      = TASK_DEFAULT_TIMESLICE;
    t->time_slice_init = TASK_DEFAULT_TIMESLICE;
    t->total_runtime   = 0;
    t->rip             = entry;
    t->is_userspace    = 1;
    t->user_rsp        = user_rsp;
    t->cr3             = cr3;

    strncpy(t->name, name, sizeof(t->name) - 1);

    t->rsp = alloc_and_init_stack(t);
    if (!t->rsp) { free(t); return NULL; }

    t->fpu_state = (fpu_state_t*)pmm_alloc_zero(1);

    t->next = ready_queues[t->priority];
    ready_queues[t->priority] = t;

    serial_printf("Task created (user): %s (prio %d, entry=0x%llx, user_rsp=0x%llx, cr3=0x%llx)\n",
                  t->name, t->priority, entry, user_rsp, cr3);
    return t;
}

void task_destroy(task_t* task) {
    if (!task) return;
    if (task->fpu_state) {
        pmm_free(task->fpu_state, 1);
        task->fpu_state = NULL;
    }
    if (task->stack_base) {
        pmm_free((void*)task->stack_base, KERNEL_STACK_PAGES);
        task->stack_base = 0;
    }
    free(task);
}

__attribute__((noreturn)) void task_exit(void) {
    asm volatile ("cli");
    uint32_t cpu = lapic_get_id();
    task_t* me = current_task[cpu];

    if (me) {
        me->state    = TASK_ZOMBIE;
        me->runnable = false;
    }

    serial_printf("[SCHED] task_exit: '%s' on CPU %u\n",
                  me ? me->name : "?", cpu);

    vmm_switch_pagemap(vmm_get_kernel_pagemap());
    current_task[cpu] = NULL;
    sched_reschedule();

    while (1) asm volatile ("cli; hlt");
}

void task_kill(task_t* target) {
    if (!target) return;

    asm volatile ("cli");

    target->runnable = false;
    target->state    = TASK_ZOMBIE;

    task_t* prev = NULL;
    task_t* cur  = ready_queues[target->priority];
    while (cur) {
        if (cur == target) {
            if (prev) prev->next = cur->next;
            else      ready_queues[target->priority] = cur->next;
            break;
        }
        prev = cur;
        cur  = cur->next;
    }

    for (uint32_t cpu = 0; cpu < smp_get_cpu_count(); cpu++) {
        prev = NULL;
        cur  = percpu_ready_queues[cpu][target->priority];
        while (cur) {
            if (cur == target) {
                if (prev) prev->next = cur->next;
                else      percpu_ready_queues[cpu][target->priority] = cur->next;
                break;
            }
            prev = cur;
            cur  = cur->next;
        }

        if (current_task[cpu] == target) {
            serial_printf("[SCHED] task_kill: target running on CPU %u, sending reschedule IPI\n", cpu);
            extern uint32_t smp_get_lapic_id_for_cpu(uint32_t cpu_index);
            ipi_reschedule_cpu(smp_get_lapic_id_for_cpu(cpu));
        }
    }

    asm volatile ("sti");
}

static task_t* sched_pick_next(uint32_t cpu) {
    for (int p = MAX_PRIORITY; p >= 0; p--) {
        task_t* t = percpu_ready_queues[cpu][p];
        if (t && t->runnable) {
            percpu_ready_queues[cpu][p] = t->next;
            t->next = NULL;
            return t;
        }
    }

    for (int p = MAX_PRIORITY; p >= 0; p--) {
        task_t* t = ready_queues[p];
        if (t && t->runnable) {
            ready_queues[p] = t->next;
            t->next = NULL;
            return t;
        }
    }
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
        old->state      = TASK_READY;
        old->next = percpu_ready_queues[cpu][old->priority];
        percpu_ready_queues[cpu][old->priority] = old;
    }

    if (next->cr3 != 0 && (!old || old->cr3 != next->cr3))
        asm volatile ("mov %0, %%cr3" :: "r"(next->cr3) : "memory");

    if (tss[cpu]) {
        tss[cpu]->rsp0 = next->stack_base + KERNEL_STACK_SIZE;
        percpu_t* pc = get_percpu();
        if (pc) {
            pc->syscall_kernel_rsp = tss[cpu]->rsp0;
        }
    }

    next->cpu_id  = cpu;
    next->state   = TASK_RUNNING;
    current_task[cpu] = next;

    if (next->fpu_state)
        fpu_restore(next->fpu_state);

    if (!old) {
        asm volatile ("" ::: "memory");
        if (next->is_userspace) {
            serial_printf("[SCHED] CPU %u: launching userspace '%s' "
                          "(entry=0x%llx user_rsp=0x%llx cr3=0x%llx)\n",
                          cpu, next->name,
                          (uint64_t)next->entry, next->user_rsp, next->cr3);
        }
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
    serial_printf("[SCHED] reschedule_calls=%llu\n", reschedule_calls);
    for (int p = MAX_PRIORITY; p >= 0; p--) {
        int count = 0;
        for (task_t* t = ready_queues[p]; t; t = t->next) count++;
        if (count) serial_printf("  prio %d: %d tasks\n", p, count);
    }
    for (uint32_t cpu = 0; cpu < smp_get_cpu_count(); cpu++) {
        int count = 0;
        for (int p = MAX_PRIORITY; p >= 0; p--)
            for (task_t* t = percpu_ready_queues[cpu][p]; t; t = t->next) count++;
        if (count) serial_printf("  cpu %u percpu: %d tasks\n", cpu, count);
    }
}

static void idle_loop(void* arg) {
    (void)arg;
    while (1) {
        asm volatile ("sti; hlt");
    }
}
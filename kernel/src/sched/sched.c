#include "../include/sched/sched.h"
#include "../include/sched/capabilities.h"
#include "../include/sched/spinlock.h"
#include "../include/memory/pmm.h"
#include "../include/memory/vmm.h"
#include "../include/io/serial.h"
#include "../include/smp/smp.h"
#include "../include/smp/percpu.h"
#include "../include/apic/apic.h"
#include "../include/gdt/gdt.h"
#include <string.h>
#include <stdlib.h>

#define KERNEL_STACK_PAGES (KERNEL_STACK_SIZE / 0x1000)
#define MAX_PIDS 1024

task_t* ready_queues[MAX_PRIORITY + 1] = {0};
task_t* current_task[8] = {0};

static task_t* percpu_ready_queues[8][MAX_PRIORITY + 1] = {0};
static task_t  idle_tasks[8];
static volatile uint64_t reschedule_calls = 0;
static spinlock_t ready_queue_lock = SPINLOCK_INIT;
static task_t*    pid_table[MAX_PIDS] = {0};
static uint32_t   next_pid = 1;
static spinlock_t pid_lock = SPINLOCK_INIT;

extern tss_t* tss[MAX_CPUS];

uint32_t task_alloc_pid(void) {
    spinlock_acquire(&pid_lock);
    uint32_t found = 0;
    for (uint32_t i = next_pid; i < MAX_PIDS; i++) {
        if (!pid_table[i]) { next_pid = i + 1; found = i; break; }
    }
    if (!found) {
        for (uint32_t i = 1; i < next_pid; i++) {
            if (!pid_table[i]) { next_pid = i + 1; found = i; break; }
        }
    }
    spinlock_release(&pid_lock);
    return found;
}

task_t* task_find_by_pid(uint32_t pid) {
    if (pid == 0 || pid >= MAX_PIDS) return NULL;
    return pid_table[pid];
}

static void pid_register(task_t* t) {
    if (t->pid && t->pid < MAX_PIDS) pid_table[t->pid] = t;
}

static void pid_unregister(task_t* t) {
    if (t->pid && t->pid < MAX_PIDS) pid_table[t->pid] = NULL;
}

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
    asm volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
    uintptr_t stack_top = (stack_virt + KERNEL_STACK_SIZE) & ~0xFULL;
    uint64_t* sp = (uint64_t*)stack_top;
    if (t->flags & TASK_FLAG_FORK) {
        extern void task_trampoline_fork(void);
        *--sp = (uint64_t)task_trampoline_fork;
    } else if (t->is_userspace) {
        extern void task_trampoline_user(void);
        *--sp = (uint64_t)task_trampoline_user;
    } else {
        extern void task_trampoline(void);
        *--sp = (uint64_t)task_trampoline;
    }
    *--sp = (uint64_t)t;
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;
    return (uint64_t)sp;
}

static void enqueue_global(task_t* t) {
    spinlock_acquire(&ready_queue_lock);
    t->next = ready_queues[t->priority];
    ready_queues[t->priority] = t;
    spinlock_release(&ready_queue_lock);
}

void sched_init(void) {
    memset(pid_table, 0, sizeof(pid_table));
    next_pid = 1;
    for (uint32_t i = 0; i < smp_get_cpu_count(); i++) {
        task_t* idle = &idle_tasks[i];
        memset(idle, 0, sizeof(task_t));
        idle->priority        = 0;
        idle->runnable        = true;
        idle->state           = TASK_READY;
        idle->cpu_id          = i;
        idle->last_cpu        = i;
        idle->time_slice      = 1;
        idle->time_slice_init = 1;
        idle->entry           = idle_loop;
        idle->arg             = NULL;
        idle->is_userspace    = TASK_TYPE_KERNEL;
        idle->pid             = 0;
        idle->uid             = UID_ROOT;
        idle->gid             = GID_ROOT;
        idle->capabilities    = CAP_ALL;
        idle->name[0]='i'; idle->name[1]='d';
        idle->name[2]='l'; idle->name[3]='e';
        idle->rsp = alloc_and_init_stack(idle);
        if (!idle->rsp) {
            serial_printf("[SCHED] FATAL: no idle stack CPU %u\n", i);
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
    t->pid             = task_alloc_pid();
    if (!t->pid) { free(t); return NULL; }
    t->ppid            = 0;
    t->uid             = UID_ROOT;
    t->gid             = GID_ROOT;
    t->capabilities    = CAP_ALL;
    t->entry           = entry;
    t->arg             = arg;
    t->priority        = priority > MAX_PRIORITY ? MAX_PRIORITY : priority;
    t->runnable        = true;
    t->state           = TASK_READY;
    t->cpu_id          = (uint32_t)-1;
    t->time_slice      = TASK_DEFAULT_TIMESLICE;
    t->time_slice_init = TASK_DEFAULT_TIMESLICE;
    t->rip             = (uint64_t)entry;
    t->is_userspace    = TASK_TYPE_KERNEL;
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->rsp = alloc_and_init_stack(t);
    if (!t->rsp) { free(t); return NULL; }
    t->fpu_state = (fpu_state_t*)pmm_alloc_zero(1);
    pid_register(t);
    enqueue_global(t);
    serial_printf("[SCHED] task_create: '%s' pid=%u prio=%d\n", t->name, t->pid, t->priority);
    return t;
}

task_t* task_create_user(const char* name, uintptr_t entry, uintptr_t user_rsp, uint64_t cr3, int priority, vmm_pagemap_t* pagemap, uint32_t uid,uint32_t gid) {
    task_t* t = calloc(1, sizeof(task_t));
    if (!t) return NULL;
    t->pid             = task_alloc_pid();
    if (!t->pid) { free(t); return NULL; }
    t->ppid            = 0;
    t->uid             = uid;
    t->gid             = gid;
    t->capabilities    = cap_initial(uid);
    t->entry           = (void (*)(void*))entry;
    t->arg             = NULL;
    t->priority        = priority > MAX_PRIORITY ? MAX_PRIORITY : priority;
    t->runnable        = true;
    t->state           = TASK_READY;
    t->cpu_id          = (uint32_t)-1;
    t->time_slice      = TASK_DEFAULT_TIMESLICE;
    t->time_slice_init = TASK_DEFAULT_TIMESLICE;
    t->rip             = entry;
    t->is_userspace    = TASK_TYPE_USER;
    t->user_rsp        = user_rsp;
    t->cr3             = cr3;
    t->pagemap         = pagemap;
    t->brk_start       = 0;
    t->brk_current     = 0;
    t->brk_max         = 0x0000700000000000ULL;
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->rsp = alloc_and_init_stack(t);
    if (!t->rsp) { free(t); return NULL; }
    t->fpu_state = (fpu_state_t*)pmm_alloc_zero(1);
    pid_register(t);
    enqueue_global(t);
    serial_printf("[SCHED] task_create_user: '%s' pid=%u uid=%u entry=0x%llx user_rsp=0x%llx caps=0x%llx\n", t->name, t->pid, t->uid, entry, user_rsp, t->capabilities);
    return t;
}

task_t* task_fork(task_t* parent) {
    if (!parent)
        return NULL;
    task_t* child = calloc(1, sizeof(task_t));
    if (!child)
        return NULL;
    child->pid = task_alloc_pid();
    if (!child->pid) {
        free(child);
        return NULL;
    }
    child->ppid = parent->pid;
    child->priority     = parent->priority;
    child->is_userspace = parent->is_userspace;
    strncpy(child->name, parent->name, sizeof(child->name)-1);
    child->uid          = parent->uid;
    child->gid          = parent->gid;
    child->capabilities = parent->capabilities;
    child->time_slice      = parent->time_slice_init;
    child->time_slice_init = parent->time_slice_init;
    child->pagemap = vmm_clone_pagemap(parent->pagemap);
    if (!child->pagemap) {
        free(child);
        return NULL;
    }
    child->cr3 = (uint64_t)pmm_virt_to_phys(child->pagemap->pml4);
    child->brk_start   = parent->brk_start;
    child->brk_current = parent->brk_current;
    child->brk_max     = parent->brk_max;
    percpu_t* pc = get_percpu();
    if (pc) {
        child->user_rsp = pc->syscall_user_rsp;
    } else {
        child->user_rsp = parent->user_rsp;
    }
    child->flags |= TASK_FLAG_FORK;
    child->rsp = alloc_and_init_stack(child);
    if (!child->rsp) {
        vmm_free_pagemap(child->pagemap);
        free(child);
        return NULL;
    }
    child->fpu_state = (fpu_state_t*)pmm_alloc_zero(1);
    if (child->fpu_state && parent->fpu_state) {
        memcpy(child->fpu_state, parent->fpu_state, sizeof(fpu_state_t));
        child->fpu_used = parent->fpu_used;
    }
    child->user_saved_rip = parent->user_saved_rip;
    child->user_saved_rbp = parent->user_saved_rbp;
    child->user_saved_rbx = parent->user_saved_rbx;
    child->user_saved_r12 = parent->user_saved_r12;
    child->user_saved_r13 = parent->user_saved_r13;
    child->user_saved_r14 = parent->user_saved_r14;
    child->user_saved_r15 = parent->user_saved_r15;
    child->user_saved_r11 = parent->user_saved_r11;
    child->flags |= TASK_FLAG_STARTED;
    child->state    = TASK_READY;
    child->runnable = true;
    child->parent  = parent;
    child->sibling = parent->children;
    parent->children = child;
    pid_register(child);
    enqueue_global(child);
    serial_printf("[SCHED] fork: parent='%s' pid=%u -> child pid=%u\n", parent->name, parent->pid, child->pid);
    return child;
}

void task_destroy(task_t* task) {
    if (!task) return;
    pid_unregister(task);
    if (task->fpu_state) {
        pmm_free(task->fpu_state, 1);
        task->fpu_state = NULL;
    }
    if (task->stack_base) {
        pmm_free((void*)task->stack_base, KERNEL_STACK_PAGES);
        task->stack_base = 0;
    }
    if (task->pagemap && (task->flags & TASK_FLAG_FORK)) {
        vmm_free_pagemap(task->pagemap);
        task->pagemap = NULL;
    }
    free(task);
}

void task_reparent(task_t* child, task_t* new_parent) {
    if (!child || !new_parent) return;
    child->parent  = new_parent;
    child->ppid    = new_parent->pid;
    child->sibling = new_parent->children;
    new_parent->children = child;
}

void task_wakeup_waiters(uint32_t pid) {
    extern task_t* pid_table[];
    for (uint32_t i = 1; i < 1024; i++) {
        task_t* t = pid_table[i];
        if (!t) continue;
        if (t->state != TASK_BLOCKED) continue;
        if (t->wait_for_pid != pid && t->wait_for_pid != (uint32_t)-1) continue;

        t->wait_for_pid = 0;
        t->runnable     = true;
        t->state        = TASK_READY;
        enqueue_global(t);
    }
}

__attribute__((noreturn)) void task_exit(void) {
    uint32_t cpu = lapic_get_id();
    task_t* me   = current_task[cpu];
    asm volatile("cli");

    if (me) {
        me->runnable = false;
        me->state    = TASK_ZOMBIE;
        task_t* init  = task_find_by_pid(1);
        task_t* child = me->children;
        while (child) {
            task_t* sib = child->sibling;
            if (init && init != me)
                task_reparent(child, init);
            child = sib;
        }
        serial_printf("[SCHED] task_exit: '%s' pid=%u exit=%d cpu=%u\n", me->name, me->pid, me->exit_code, cpu);
        task_wakeup_waiters(me->pid);
    }
    vmm_switch_pagemap(vmm_get_kernel_pagemap());
    current_task[cpu] = NULL;
    asm volatile("sti");
    sched_reschedule();
    while (1)
        asm volatile("hlt");
}

void task_kill(task_t* target) {
    if (!target) return;
    asm volatile("cli");
    target->runnable = false;
    target->state    = TASK_ZOMBIE;
    spinlock_acquire(&ready_queue_lock);
    task_t* prev = NULL;
    task_t* cur  = ready_queues[target->priority];
    while (cur) {
        if (cur == target) {
            if (prev) prev->next = cur->next;
            else      ready_queues[target->priority] = cur->next;
            break;
        }
        prev = cur; cur = cur->next;
    }
    spinlock_release(&ready_queue_lock);
    for (uint32_t cpu = 0; cpu < smp_get_cpu_count(); cpu++) {
        prev = NULL;
        cur  = percpu_ready_queues[cpu][target->priority];
        while (cur) {
            if (cur == target) {
                if (prev) prev->next = cur->next;
                else percpu_ready_queues[cpu][target->priority] = cur->next;
                break;
            }
            prev = cur; cur = cur->next;
        }
        if (current_task[cpu] == target) {
            extern uint32_t smp_get_lapic_id_for_cpu(uint32_t);
            ipi_reschedule_cpu(smp_get_lapic_id_for_cpu(cpu));
        }
    }
    asm volatile("sti");
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

    spinlock_acquire(&ready_queue_lock);
    task_t* found = NULL;
    for (int p = MAX_PRIORITY; p >= 0; p--) {
        task_t* t = ready_queues[p];
        if (t && t->runnable) {
            ready_queues[p] = t->next;
            t->next = NULL;
            found = t;
            break;
        }
    }
    spinlock_release(&ready_queue_lock);
    return found ? found : &idle_tasks[cpu];
}

void sched_reschedule(void) {
    reschedule_calls++;
    uint32_t cpu  = lapic_get_id();
    task_t*  old  = current_task[cpu];
    task_t*  next = sched_pick_next(cpu);

    if (!next)
        return;

    if (old == next) {
        if (old)
            old->time_slice = old->time_slice_init;
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

    if (next->cr3 && (!old || old->cr3 != next->cr3)) {
        asm volatile("mov %0, %%cr3" :: "r"(next->cr3) : "memory");
    }

    if (tss[cpu]) {
        tss[cpu]->rsp0 = next->stack_base + KERNEL_STACK_SIZE;
        percpu_t* pc = get_percpu();
        if (pc) {
            pc->syscall_kernel_rsp = tss[cpu]->rsp0;
            if (next->is_userspace) {
                pc->syscall_user_rsp = next->user_rsp;
            }
        }
    }
    next->cpu_id = cpu;
    next->state  = TASK_RUNNING;
    current_task[cpu] = next;
    if (next->fpu_state)
        fpu_restore(next->fpu_state);

    if (!(next->flags & TASK_FLAG_STARTED)) {
        next->flags |= TASK_FLAG_STARTED;
        if (next->is_userspace) {
            serial_printf(
                "[SCHED] CPU %u: first start '%s' pid=%u entry=0x%llx user_rsp=0x%llx\n",
                cpu,
                next->name,
                next->pid,
                (uint64_t)next->entry,
                next->user_rsp
            );
        }
        first_task_start(next);
        __builtin_unreachable();
    }

    if (old) {
        context_switch(old, next);
    } else {
        static task_t bootstrap_task;
        context_switch(&bootstrap_task, next);
    }
}

void task_yield(void) {
    sched_reschedule();
}

void sched_print_stats(void) {
    serial_printf("[SCHED] reschedule_calls=%llu\n", reschedule_calls);
    spinlock_acquire(&ready_queue_lock);
    for (int p = MAX_PRIORITY; p >= 0; p--) {
        int n = 0;
        for (task_t* t = ready_queues[p]; t; t = t->next) n++;
        if (n) serial_printf("  prio %d: %d tasks\n", p, n);
    }
    spinlock_release(&ready_queue_lock);
}

static void idle_loop(void* arg) {
    (void)arg;
    while (1) {
        asm volatile("hlt");
    }
}